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
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "stb_vorbis.c"

// structs
struct PixelRGB { uint8_t r, g, b; };

inline void MultiplyBlend(const PixelRGB& a, const PixelRGB& b, PixelRGB& out )
{
	out.r = static_cast<uint8_t>((a.r * b.r) / 255);
	out.g = static_cast<uint8_t>((a.g * b.g) / 255);
	out.b = static_cast<uint8_t>((a.b * b.b) / 255);
}

// Generate logarithmically spaced scales
static void logScales(std::vector<float>& scales, float min_scale, float max_scale, int num_scales)
{
	scales.resize(num_scales);
	const float log_min = std::log(min_scale);
	const float log_max = std::log(max_scale);
	for (int i = 0; i < num_scales; i++)
	{
		scales[i] = std::exp(log_min + i * (log_max - log_min) / (num_scales - 1));
	}
}

static void Convolve( const std::vector<float>& signal, int startsample, const std::vector<float>& kernel, std::vector<float>& output)
{
	const int signal_size = static_cast<int>(signal.size());
	const int kernel_size = static_cast<int>(kernel.size());
	const int half_kernel = kernel_size / 2;
	const int spectrogram_samples = output.size();
	output.resize(spectrogram_samples, 0.0);
	for (int i = 0; i < spectrogram_samples; i++)
	{
		float sum = 0.0;
		for (int k = 0; k < kernel_size; k++)
		{
			int signal_index = startsample + i + k - half_kernel;
			if (signal_index >= 0 && signal_index < signal_size)
			{
				sum += signal[signal_index] * kernel[k];
			}
		}
		output[i] = sum;
	}
}

static void GenerateMorlet(std::vector<float>& kernel, int num_samples, float k ) 
{
	kernel.resize(num_samples);
	const float falloff = 0.5f;
	const float extent = 6.0f;
	for (int i = 0; i < num_samples; i++)
	{
		const float t = float(i)/num_samples * extent * 2.0f - extent;
		kernel[i] = std::exp(-std::abs( t * falloff) ) * std::cos(3.1415f * 2.0f * t * k);
	}
}

int main(int argc, char** argv)
{
	// Read input args

	std::string input_filename;
	std::string output_filename_prefix;
	std::string heatmap_filename;
	std::string gradientmap_filename;
	int image_width = 1920;
	int image_height = 1080;
	int spectrogram_samples = 0;
	float input_start_skip = 0.0f;
	int num_frames = 1;
	int sample_offset = 24;

	if( argc < 2 )
	{
		std::cout << "Usage:\n   " << argv[0] << "<input audio file> <output filename prefix> [options]\n\nOptions:\n"
			"   -w       <width>    Width of the output image(s) [default: 1920]\n"
			"   -h       <height>   Height of the output image(s) [default: 1080]\n"
			"   -heatmap <filename> Path to the heatmap image file\n"
			"   -gradmap <filename> Path to the gradiantmap image file\n"
			"   -samples <samples>  Specgram sample resolution [default: same as image width]\n"
			"   -skip    <seconds>  Time in the input audio file where sampling begins [default: 0]\n"
			"   -frames  <frames>   Number of images to generate [default: 1]\n"
			"   -offset  <samples>  Number of samples to progress for each frame [default: 24]\n";
		return 0;
	}

	for (int i = 1; i < argc; i++)
	{
		std::string arg = argv[i];
		if( i == 1 && i + 1 < argc )
		{
			input_filename = argv[i];
		}
		else if( i == 2 && i + 1 < argc )
		{
			output_filename_prefix = argv[i];
		}
		else if ( strcmp( argv[i], "-w" ) == 0 && i + 1 < argc)
		{
			std::string value = argv[++i];
			image_width = std::stoi( value );
		}
		else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc)
		{
			std::string value = argv[++i];
			image_height = std::stoi(value);
		}
		else if (strcmp(argv[i], "-heatmap") == 0 && i + 1 < argc)
		{
			heatmap_filename = argv[++i];
		}
		else if (strcmp(argv[i], "-gradmap") == 0 && i + 1 < argc)
		{
			gradientmap_filename = argv[++i];
		}
		else if (strcmp(argv[i], "-samples") == 0 && i + 1 < argc)
		{
			std::string value = argv[++i];
			spectrogram_samples = std::stoi(value);
		}
		else if (strcmp(argv[i], "-skip") == 0 && i + 1 < argc)
		{
			std::string value = argv[++i];
			input_start_skip = std::stof(value);
		}
		else if (strcmp(argv[i], "-frames") == 0 && i + 1 < argc)
		{
			std::string value = argv[++i];
			num_frames = std::stoi(value);
		}
		else if (strcmp(argv[i], "-offset") == 0 && i + 1 < argc)
		{
			std::string value = argv[++i];
			sample_offset = std::stoi(value);
		}
	}

	if (spectrogram_samples == 0)
	{
		spectrogram_samples = image_width;
	}

	// generate the heatmap

	constexpr int HEATMAP_SIZE = 256;
	std::vector<PixelRGB> heatmap;
	heatmap.resize(HEATMAP_SIZE);
	if( !heatmap_filename.empty() )
	{
		// load heatmap from file (expects a horizontal gradient, will be stretched to fit the heatmap size)

		unsigned char* colourmap_data = nullptr;
		int colourmap_width = 0;
		int colourmap_height = 0;
		int color_channels = 0;

		colourmap_data = stbi_load(heatmap_filename.c_str(), &colourmap_width, &colourmap_height, &color_channels, 3);
		if(colourmap_data == nullptr)
		{
			std::cerr << "Error loading heatmap image: " << heatmap_filename << std::endl;
			return 1;
		}
		for (int i = 0; i < HEATMAP_SIZE; i++)
		{
			int x = (i * colourmap_width) / HEATMAP_SIZE;
			heatmap[i].r = colourmap_data[(x * 3) + 0];
			heatmap[i].g = colourmap_data[(x * 3) + 1];
			heatmap[i].b = colourmap_data[(x * 3) + 2];
		}
		stbi_image_free(colourmap_data);
	}
	else
	{
		// default colourmap (grayscale)

		for (int i = 0; i < HEATMAP_SIZE; i++)
		{
			heatmap[i].r = static_cast< float >( i ) / HEATMAP_SIZE * 255;
			heatmap[i].g = static_cast< float >( i ) / HEATMAP_SIZE * 255;
			heatmap[i].b = static_cast< float >( i ) / HEATMAP_SIZE * 255;
		}
	}
	
	// Generate the gradiantmap
	
	constexpr int GRADIENTMAP_SIZE = 256;
	std::vector< std::vector<PixelRGB>> gradientmap;
	gradientmap.resize(GRADIENTMAP_SIZE);
	if( !gradientmap_filename.empty() )
	{
		// Load the gradiantmap from file (expects a 2D gradient, will be stretched to fit the gradiantmap size)

		unsigned char* gradientmap_data = nullptr;
		int gradientmap_width = 0;
		int gradientmap_height = 0;
		int color_channels = 0;

		gradientmap_data = stbi_load(gradientmap_filename.c_str(), &gradientmap_width, &gradientmap_height, &color_channels, 3);
		for (int i = 0; i < GRADIENTMAP_SIZE; i++)
		{
			int y = (i * gradientmap_height) / GRADIENTMAP_SIZE;
			gradientmap[i].resize(GRADIENTMAP_SIZE);
			for (int x = 0; x < gradientmap_width; x++)
			{
				gradientmap[i][x].r = gradientmap_data[(y * gradientmap_width * 3) + (x * 3) + 0];
				gradientmap[i][x].g = gradientmap_data[(y * gradientmap_width * 3) + (x * 3) + 1];
				gradientmap[i][x].b = gradientmap_data[(y * gradientmap_width * 3) + (x * 3) + 2];
			}
		}
		stbi_image_free(gradientmap_data);
	}
	else
	{
		// default gradiantmap (none)

		for (int i = 0; i < GRADIENTMAP_SIZE; i++)
		{
			gradientmap[i].resize(GRADIENTMAP_SIZE);
			gradientmap[i].push_back(PixelRGB{ 255,255,255 });
		}
	}

	// Read the audio file
	
	int audio_sample_rate = 0;
	int audio_channels = 0;
	int audio_samples = 0;
	short* audio_data = nullptr;

	audio_samples = stb_vorbis_decode_filename(input_filename.c_str(), &audio_channels, &audio_sample_rate, &audio_data);
	if (audio_samples < 0)
	{
		std::cerr << "Error reading audio file: " << audio_samples << std::endl;
		return 1;
	}
	if( audio_channels != 1 )
	{
		free(audio_data);
		std::cerr << "Error: Only mono audio files are supported." << std::endl;
		return 1;
	}

	// convert samples to float

	std::vector<float> signalvector;
	for (int i = 0; i < audio_samples; i++)
	{
		signalvector.push_back( static_cast<float>(audio_data[i]) / std::_Max_limit< short >() );
	}

	free(audio_data);

	// Generate wavelets
	
	std::vector< std::vector<float> > cwt_coeffs;
    std::vector<float> wavlet_kernel;

    std::vector< float > scales;
    logScales( scales, 1.0f, 3.0f, image_height );

	std::string outputfilename;

	const int signallen = static_cast<int>(signalvector.size());
	const int sample = input_start_skip * audio_sample_rate;
	if( sample + num_frames * sample_offset + spectrogram_samples > signallen )
	{
		std::cerr << "Error: Not enough audio samples for the requested number of frames and sample skip." << std::endl;
		return 1;
	}
	
	constexpr int IMAGE_CHANNELS = 3;
	unsigned char* image_data = new unsigned char[image_width * image_height * IMAGE_CHANNELS];
	
	for (int s = 0; s < num_frames; ++s)
	{
		cwt_coeffs.clear();
		cwt_coeffs.reserve(image_height);

		// Generate the spectrogram coefficients using CWT with Morlet wavelets at different scales.

		for (int i = 0; i < image_height; i++)
		{
			cwt_coeffs.emplace_back(std::vector<float>(spectrogram_samples));

			const int in_kernel_size = image_width * 0.25f;
			// flip the scales so that higher frequencies are at the top of the image
			const float scale = scales[image_height - (i + 1)];
			GenerateMorlet(wavlet_kernel, in_kernel_size, scale - 1);
			Convolve( signalvector, sample + s * sample_offset, wavlet_kernel, cwt_coeffs.back() );
		}

		// Generate the image

		const float width_scale = (cwt_coeffs[0].size() - 1) / static_cast<float>(image_width - 1);
		const float height_scale = (cwt_coeffs.size() - 1) / static_cast<float>(image_height);

		// Normalise
		float maxVal = 0.0f;
		for (const auto& row : cwt_coeffs)
		{
			for (float v : row)
			{
				maxVal = std::max(maxVal, abs(v));
			}
		}
		if (maxVal == 0.0f) maxVal = 1.0f;

		// Bayer matrix for dithering (4x4)
		constexpr int BAYER_SIZE = 4;
		constexpr float BAYER4[BAYER_SIZE][BAYER_SIZE] = {
			{ 0.0f, 0.5f, 0.125f, 0.625f },
			{ 0.75f, 0.25f, 0.875f, 0.375f },
			{ 0.1875f, 0.6875f, 0.0625f, 0.5625f },
			{ 0.9375f, 0.4375f, 0.8125f, 0.3125f } };

		PixelRGB* pixelData = reinterpret_cast<PixelRGB*>(image_data);
		for (int i = 0; i < image_height; i++)
		{
			for (int j = 0; j < image_width; j++)
			{
				const int coefx = static_cast<int>(j * width_scale);
				const int coefy = static_cast<int>(i * height_scale);

				float val = 0.0;

				if (width_scale > 1 || j == image_width - 1)
				{
					const float normval = cwt_coeffs[coefy][coefx] / maxVal;
					val = (normval + 1) * 0.5f;
				}
				else
				{
					// Bilinear interpolation if the spectrogram has less horizontal resolution than the output image
					float frac = (j * width_scale) - coefx;
					float interp = std::lerp(cwt_coeffs[coefy][coefx], cwt_coeffs[coefy][coefx + 1], frac);
					const float normval = interp / maxVal;
					val = (normval + 1) * 0.5f;
				}

				PixelRGB& pixel = pixelData[i * image_width + j];

				// Dithering using Bayer matrix to reduce banding artifacts when mapping the spectrogram values to the
				// heatmap and gradiantmap
				const float bayerval = BAYER4[j % BAYER_SIZE][i % BAYER_SIZE];
				float heatmapidxfloat = val * (HEATMAP_SIZE - 1);
				float frac = heatmapidxfloat - static_cast<int>(heatmapidxfloat);
				int offset = (frac > bayerval) ? 1 : 0;
				const int heatmapidx = std::min(static_cast<int>(heatmapidxfloat) + offset, HEATMAP_SIZE - 1);

				float gradientmapsamplexfloat = static_cast<float>(j) / image_width * (GRADIENTMAP_SIZE - 1);
				frac = gradientmapsamplexfloat - static_cast<int>(gradientmapsamplexfloat);
				offset = (frac > bayerval) ? 1 : 0;
				const int gradientmapsamplex = std::min(static_cast<int>(gradientmapsamplexfloat) + offset, HEATMAP_SIZE - 1);

				float gradientmapsampleyfloat = static_cast<float>(i) / image_height * (GRADIENTMAP_SIZE - 1);
				frac = gradientmapsampleyfloat - static_cast<int>(gradientmapsampleyfloat);
				offset = (frac > bayerval) ? 1 : 0;
				const int gradientmapsampley = std::min(static_cast<int>(gradientmapsampleyfloat) + offset, HEATMAP_SIZE - 1);

				MultiplyBlend(heatmap[heatmapidx], gradientmap[gradientmapsampley][gradientmapsamplex], pixel);
			}
		}

		// Write to file

		outputfilename = output_filename_prefix;
		outputfilename.append(std::to_string(s));
		outputfilename.append(".png");

		stbi_write_png(outputfilename.c_str(), image_width, image_height, IMAGE_CHANNELS, image_data, image_width * IMAGE_CHANNELS);
	}

	delete[] image_data;

	return 0;
}
