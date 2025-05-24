#include <iostream>
#include <vector>
#include <fstream>
#include <png.h>
#include <cmath>

const int MAX_OUTPUT_CHARS = 190000; // Maximum allowed characters in the output file

void resize_image(const std::vector<png_bytep>& input, std::vector<png_bytep>& output, int old_width, int old_height, int new_width, int new_height) {
    for (int y = 0; y < new_height; y++) {
        for (int x = 0; x < new_width; x++) {
            // Calculate the position in the original image
            float src_x = x * (static_cast<float>(old_width) / new_width);
            float src_y = y * (static_cast<float>(old_height) / new_height);

            int x0 = static_cast<int>(src_x);
            int y0 = static_cast<int>(src_y);
            int x1 = std::min(x0 + 1, old_width - 1);
            int y1 = std::min(y0 + 1, old_height - 1);

            float x_weight = src_x - x0;
            float y_weight = src_y - y0;

            for (int c = 0; c < 3; c++) { // RGB channels
                // Perform bilinear interpolation
                float top = (1 - x_weight) * input[y0][x0 * 3 + c] + x_weight * input[y0][x1 * 3 + c];
                float bottom = (1 - x_weight) * input[y1][x0 * 3 + c] + x_weight * input[y1][x1 * 3 + c];
                output[y][x * 3 + c] = static_cast<png_byte>((1 - y_weight) * top + y_weight * bottom);
            }
        }
    }
}

float calculate_max_scale_factor(int width, int height) {
    const int chars_per_pixel = 25; // Approximate characters per pixel in the output format
    int max_pixels = MAX_OUTPUT_CHARS / chars_per_pixel;

    // Dynamically calculate the margin to better utilize the available space
    float margin = 0.95f; // Adjust this value closer to 1.0f for better utilization
    float max_scale_factor = std::sqrt(static_cast<float>(max_pixels) / (width * height)) * margin;
    return max_scale_factor;
}

int estimate_output_size(int width, int height, float scale_factor) {
    int new_width = static_cast<int>(width * scale_factor);
    int new_height = static_cast<int>(height * scale_factor);
    return new_width * new_height * 25; // Approximate characters per pixel in the output format
}

void read_png_file(const char* filename, const char* output_filename) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        std::cerr << "Error: Unable to open file " << filename << std::endl;
        return;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        std::cerr << "Error: Unable to create PNG read struct" << std::endl;
        fclose(fp);
        return;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        std::cerr << "Error: Unable to create PNG info struct" << std::endl;
        png_destroy_read_struct(&png, nullptr, nullptr);
        fclose(fp);
        return;
    }

    if (setjmp(png_jmpbuf(png))) {
        std::cerr << "Error: During PNG read" << std::endl;
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(fp);
        return;
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    int width = png_get_image_width(png, info);
    int height = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);

    double gamma = 1.5; // Adjust based on your display
    double file_gamma = 1.0; // Default gamma value

    if (png_get_gAMA(png, info, &file_gamma)) {
        png_set_gamma(png, gamma, file_gamma);
    } else {
        std::cerr << "Warning: gAMA chunk not found. Using default gamma correction." << std::endl;
        png_set_gamma(png, gamma, 1.0); // Use default gamma for the file
    }

    // Ensure 8-bit depth and RGB format
    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGBA) png_set_strip_alpha(png); // Strip alpha channel if present

    png_read_update_info(png, info);

    std::vector<png_bytep> row_pointers(height);
    for (int y = 0; y < height; y++) {
        row_pointers[y] = (png_bytep)malloc(png_get_rowbytes(png, info));
    }

    png_read_image(png, row_pointers.data());

    // Calculate the maximum scale factor
    float scale_factor = calculate_max_scale_factor(width, height);

    // Clamp the scale factor to a maximum of 1.0
    if (scale_factor > 1.0f) {
        scale_factor = 1.0f;
    }

    // Estimate the output size and adjust the scale factor if necessary
    int estimated_size = estimate_output_size(width, height, scale_factor);
    if (estimated_size > MAX_OUTPUT_CHARS) {
        std::cerr << "Warning: Adjusting scale factor to fit within output size limit." << std::endl;
        scale_factor *= std::sqrt(static_cast<float>(MAX_OUTPUT_CHARS) / estimated_size);
    }

    // Calculate new dimensions based on scale factor
    int new_width = static_cast<int>(width * scale_factor);
    int new_height = static_cast<int>(height * scale_factor);

    // Resize the image
    std::vector<png_bytep> resized_row_pointers(new_height);
    for (int y = 0; y < new_height; y++) {
        resized_row_pointers[y] = (png_bytep)malloc(new_width * 3); // Only RGB channels
    }
    resize_image(row_pointers, resized_row_pointers, width, height, new_width, new_height);

    // Open the output file
    std::ofstream output_file(output_filename);
    if (!output_file.is_open()) {
        std::cerr << "Error: Unable to open output file " << output_filename << std::endl;
        for (int y = 0; y < height; y++) {
            free(row_pointers[y]);
        }
        for (int y = 0; y < new_height; y++) {
            free(resized_row_pointers[y]);
        }
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(fp);
        return;
    }

    // Write resized pixel data (RGB format) to the file
    output_file << "Image Data (RGB):" << std::endl;
    for (int y = 0; y < new_height; y++) {
        for (int x = 0; x < new_width * 3; x += 3) {
            output_file << "14,0,0," 
                        << (new_height - 1 - y) << "," << x / 3 << ","
                        << (int)resized_row_pointers[y][x] << "+" 
                        << (int)resized_row_pointers[y][x + 1] << "+" 
                        << (int)resized_row_pointers[y][x + 2] << "+2+0";
            // Add a semicolon only if it's not the last pixel
            if (!(y == new_height - 1 && x == (new_width - 1) * 3)) {
                output_file << ";";
            }
        }
    }
    output_file << "???";
    // Close the output file
    output_file.close();

    // Free memory
    for (int y = 0; y < height; y++) {
        free(row_pointers[y]);
    }
    for (int y = 0; y < new_height; y++) {
        free(resized_row_pointers[y]);
    }
    png_destroy_read_struct(&png, &info, nullptr);
    fclose(fp);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input_filename> [output_filename]" << std::endl;
        return 1;
    }

    const char* input_filename = argv[1];
    const char* output_filename = (argc >= 3) ? argv[2] : "output.txt"; // Default output filename if not provided

    read_png_file(input_filename, output_filename);
    return 0;
}