// Silence MSVC deprecation warnings used inside third-party headers
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// Lib
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <vector>

// stb
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../stb/stb_image_write.h"
#include "../stb/stb_vorbis.c"

// constants

const int WIDTH = 1280;
const int HEIGHT = 720;
const int CHANNELS = 3;
const int SIZE = WIDTH * HEIGHT * CHANNELS;
const char* OUTPUT_FILENAME = "output/frame";
const char* INPUT_FILENAME = "input3.ogg";
const int SPECTROGRAM_SAMPLES = 1024;
const int NUM_FRAMES = 128;
const int SAMPLE_SKIP = 735;

// global vars

unsigned char* image_data = nullptr;

int audio_sample_rate = 0;
int audio_channels = 0;
int audio_samples = 0;
short* audio_data = nullptr;

struct PixelRGB 
{
    uint8_t r, g, b;
};

// Map a normalized [0,1] value to a heatmap color (black -> blue -> cyan -> green -> yellow -> red)
PixelRGB heatmapColor(double v) {
    v = std::clamp(v, 0.0, 1.0);
    double r = 0, g = 0, b = 0;

    if (v < 0.25) {
        b = v / 0.25;
    }
    else if (v < 0.5) {
        b = 1.0;
        g = (v - 0.25) / 0.25;
    }
    else if (v < 0.75) {
        b = 1.0 - (v - 0.5) / 0.25;
        g = 1.0;
    }
    else {
        g = 1.0;
        r = (v - 0.75) / 0.25;
    }

    return { static_cast<uint8_t>(r * 255),
             static_cast<uint8_t>(g * 255),
             static_cast<uint8_t>(b * 255) };
}

// --- Utility: generate logarithmically spaced scales ---
void logScales(std::vector<double>& scales, double minScale, double maxScale, int numScales) {
    scales.resize(numScales);
    double logMin = std::log(minScale);
    double logMax = std::log(maxScale);
    for (int i = 0; i < numScales; i++)
    {
        scales[i] = std::exp(logMin + i * (logMax - logMin) / (numScales - 1));
    }
}

void GenerateGaussianKernel(std::vector<double>& kernel, int size, double sigma) {
    kernel.resize(size);
    int half = size / 2;
    double sum = 0.0;
    for (int i = 0; i < size; i++) {
        double x = i - half;
        kernel[i] = std::exp(-0.5 * (x * x) / (sigma * sigma));
        sum += kernel[i];
    }
    // Normalize kernel to sum to 1
    for (double& v : kernel) v /= sum;
}

void generate_image(std::vector< std::vector<double> >& coeffs)
{
    // --- 3. Normalize to [0, 1] across the entire scalogram ---
    double maxVal = 0.0;
    for (const auto& row : coeffs)
        for (double v : row)
            maxVal = std::max(maxVal, v);

    if (maxVal == 0.0) maxVal = 1.0; // guard against silent signal

	const float widthScale = ( coeffs[0].size() - 1 ) / static_cast<float>(WIDTH - 1);
	const float heightScale = ( coeffs.size() - 1 ) / static_cast<float>(HEIGHT);
    
    std::vector<double> gaussiankernel;
	GenerateGaussianKernel(gaussiankernel, ceil(widthScale*4), 4.0);
	const int gauss_kernel_size = static_cast<int>(gaussiankernel.size());

	PixelRGB* pixelData = reinterpret_cast<PixelRGB*>(image_data);
    for (int i = 0; i < HEIGHT; i++)
    {
        for (int j = 0; j < WIDTH; j++)
        {
			const int coefx = static_cast<int>(j * widthScale);
            const int coefy = static_cast<int>(i * heightScale);


			double val = 0.0;
			const double normval = coeffs[coefy][coefx] / maxVal;
            val = (normval + 1) * 0.5;
#if 0
            for( int k = 0; k < gauss_kernel_size; k++)
            {
                int samplex = coefx + k - gauss_kernel_size / 2;
                if (samplex >= 0 && samplex < static_cast<int>(coeffs[coefy].size()))
                {
                    const double normval = coeffs[coefy][samplex] / maxVal;
                    const double posval = (normval + 1) * 0.5; // map [-1,1] to [0,1]
                    val += posval * gaussiankernel[k];
                }
			}
#endif

            PixelRGB& pixel = pixelData[i * WIDTH + j];
            pixel.r = val * 255; // heatmapColor(posval);
            pixel.b = val * 255;
            pixel.g = val * 255;
        }
        
    }
}

static int readsignal()
{
    const int result = stb_vorbis_decode_filename(INPUT_FILENAME, &audio_channels, &audio_sample_rate, &audio_data);
	assert(audio_channels == 1); // only mono supported for now
    if (result < 0) return result;
    audio_samples = result;
    // audio_samples = WIDTH*0.5;
    return 0;
}

static void convolve( const std::vector<double>& signal, int startsample, const std::vector<double>& kernel, std::vector<double>& output) {
    int signalSize = static_cast<int>(signal.size());
    int kernelSize = static_cast<int>(kernel.size());
    int halfKernel = kernelSize / 2;
    output.resize(SPECTROGRAM_SAMPLES, 0.0);
    for (int i = 0; i < SPECTROGRAM_SAMPLES; i++)
    {
        double sum = 0.0;
        for (int k = 0; k < kernelSize; k++)
        {
            int signalIndex = startsample + i + k - halfKernel;
            if (signalIndex >= 0 && signalIndex < signalSize)
            {
                sum += signal[signalIndex] * kernel[k];
            }
        }
        output[i] = sum;
    }
}

static void generatemorlet(std::vector<double>& kernel, int num_samples, double k ) {
    kernel.resize(num_samples);
    const double falloff = 0.5;
    const double extent = 6;
    for (int i = 0; i < num_samples; i++) {
        double t = double(i)/num_samples * extent * 2 - extent;
        kernel[i] = std::exp(-std::abs( t * falloff) ) * std::cos(3.1415 * 2 * t * k);
    }
}

int main(int argc, char** argv)
{
    image_data = new unsigned char[SIZE];

    int result = 0;

    result = readsignal();
    if( result < 0 )
    {
        std::cerr << "Error reading audio file: " << result << std::endl;
        return 1;
    }
    std::vector<double> signalvector;
	for (int i = 0; i < audio_samples; i++)
    {
        signalvector.push_back( static_cast<double>(audio_data[i]) / std::_Max_limit< short >() );
    }

    std::vector< std::vector<double> > cwtCoeffs;
    std::vector<double> kernel;

    int signallen = static_cast<int>(signalvector.size());

    std::vector< double > scales;
    logScales(scales, 1.1, 4.5, HEIGHT);

	char outputfilename[256];

    for (int s = 0; s < NUM_FRAMES; ++s)
    {
        cwtCoeffs.clear();
        cwtCoeffs.reserve(HEIGHT);

        for (int i = 0; i < HEIGHT; i++)
        {
            cwtCoeffs.emplace_back(std::vector<double>(SPECTROGRAM_SAMPLES));

            const int in_kernel_size = WIDTH * 0.25;
            // flip the scales so that higher frequencies are at the top of the image
            const double scale = scales[HEIGHT - (i + 1)];
            generatemorlet(kernel, in_kernel_size, scale - 1);
            convolve(signalvector, s * SAMPLE_SKIP, kernel, cwtCoeffs.back());
        }
        generate_image(cwtCoeffs);
		sprintf(outputfilename, "%s_%04d.png", OUTPUT_FILENAME, s);
        stbi_write_png(outputfilename, WIDTH, HEIGHT, CHANNELS, image_data, WIDTH * CHANNELS);
    }

    free( audio_data );
    delete[] image_data;

    return 0;
}
