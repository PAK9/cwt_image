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

const int WIDTH = 512;
const int HEIGHT = 512;
const int CHANNELS = 3;
const int SIZE = WIDTH * HEIGHT * CHANNELS;
const char* OUTPUT_FILENAME = "output.png";
const char* INPUT_FILENAME = "input.ogg";

// global vars

unsigned char* image_data = nullptr;

int audio_sample_rate = 0;
int audio_channels = 0;
int audio_samples = 0;
short* audio_data = nullptr;


// Morlet wavelet: psi(t) = exp(i*omega0*t) * exp(-t^2/2)
std::complex<double> morletWavelet(double t, double omega0 = 6.0)
{
    return std::exp(std::complex<double>(0, omega0 * t)) * std::exp(-0.5 * t * t);
}

// Perform CWT at a single scale 'a' and translation 'b'
// W(a,b) = (1/sqrt(a)) * integral[ x(t) * conj(psi((t-b)/a)) dt ]
double cwtCoefficient(const std::vector<double>& signal, double a, int b, double dt = 1.0) {
    std::complex<double> result(0.0, 0.0);
    double norm = 1.0 / std::sqrt(a);
    int N = static_cast<int>(signal.size());

    for (int t = 0; t < N; t++) {
        double tau = (t - b) * dt / a;
        // Morlet wavelet has negligible support beyond |tau| > 5
        if (std::abs(tau) > 5.0) continue;
        result += signal[t] * std::conj(morletWavelet(tau)) * dt;
    }

    return std::abs(result) * norm;
}

struct PixelRGB {
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

/**
 * Continuous Wavelet Transform -> 2D pixel array
 *
 * @param signal     Input digital signal samples
 * @param scales     Vector of scales to analyze (e.g. log-spaced from 1 to 64)
 * @param dt         Sampling interval in seconds (default 1.0)
 * @param logScale   Apply log10 to magnitudes before normalizing (improves visibility)
 *
 * @return           Row-major RGBA pixel buffer [numScales rows x signalLength cols]
 *                   Row 0 = highest scale (lowest frequency) at top
 */
void generatecwt(
    const std::vector<double>& signal,
    const std::vector<double>& scales,
	std::vector< std::vector<double> >& coeffs,
    double dt = 1.0,
    bool logScale = true)
{
    int N = static_cast<int>(signal.size());
    int S = static_cast<int>(scales.size());

    coeffs.reserve(S);
	for (int i = 0; i < S; i++)
    {
        coeffs.emplace_back( std::vector<double>(N) );
    }

    // --- 1. Compute all CWT coefficients ---

    for (int si = 0; si < S; si++) {
        double a = scales[si];
        for (int b = 0; b < N; b++) {
            coeffs[si][b] = cwtCoefficient(signal, a, b, dt);
        }
    }

    // --- 2. Optional log scaling ---
    if (logScale) {
        for (auto& row : coeffs)
            for (auto& v : row)
                v = std::log10(1.0 + v);
    }
}

// --- Utility: generate logarithmically spaced scales ---
std::vector<double> logScales(double minScale, double maxScale, int numScales) {
    std::vector<double> scales(numScales);
    double logMin = std::log(minScale);
    double logMax = std::log(maxScale);
    for (int i = 0; i < numScales; i++)
        scales[i] = std::exp(logMin + i * (logMax - logMin) / (numScales - 1));
    return scales;
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
    for (int i = 0; i < HEIGHT; i++)
    {
        for (int j = 0; j < WIDTH; j++)
        {
			const int coefx = static_cast<int>(j * widthScale);
            const int coefy = static_cast<int>(i * heightScale);

			const double normalizedValue = coeffs[coefy][coefx] / maxVal;
			image_data[i * WIDTH + j] = static_cast<unsigned char>(normalizedValue * 255.0f);
			image_data[i * CHANNELS + j + 1] = static_cast<unsigned char>(normalizedValue * 255.0f);
            image_data[i * CHANNELS + j + 2] = static_cast<unsigned char>(normalizedValue * 255.0f);
        }
        
    }
}

static int readsignal()
{
    const int result = stb_vorbis_decode_filename(INPUT_FILENAME, &audio_channels, &audio_sample_rate, &audio_data);
	assert(audio_channels == 1); // only mono supported for now
    if (result < 0) return result;
    audio_samples = result;
    return 0;
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
        signalvector.push_back( static_cast<double>(audio_data[i]) );
    }
	const int numscales = 128;
	std::vector< std::vector<double> > cwtCoeffs;
    generatecwt(signalvector, logScales(1, 64, numscales), cwtCoeffs, 1.0, true);
	generate_image( cwtCoeffs );

    stbi_write_png(OUTPUT_FILENAME, WIDTH, HEIGHT, CHANNELS, image_data, WIDTH * CHANNELS);

    free( audio_data );
    delete[] image_data;

    return 0;
}
