## CWT Image

Generates images using the Continuous Wavelet Transform. The images are produced for artistic interest rather than analytical purposes.

The project depends on [Nothings/STB](https://github.com/nothings/stb), specifically:-
- stb_image.h
- stb_image_write.h
- stb_vorbis.c

The program will produce an image, or sequence of images, which are a specgram of an ogg audio file. You may provide a heatmap image, which will colourmap the specgram. You may also provide a gradiantmap image, which will be multiply blended over the generated images. Other command line switches allow for specifying the width, height, specgram resolution, offset into the audio file, number of images to generate, and the number of audio samples to skip between images.

```
Usage:
   cwt_image.exe<input audio file> <output filename prefix> [options]

Options:
   -w       <width>    Width of the output image(s) [default: 1920]
   -h       <height>   Height of the output image(s) [default: 1080]
   -heatmap <filename> Path to the heatmap image file
   -gradmap <filename> Path to the gradiantmap image file
   -samples <samples>  Specgram sample resolution [default: same as image width]
   -skip    <seconds>  Time in the input audio file where sampling begins [default: 0]
   -frames  <frames>   Number of images to generate [default: 1]
   -offset  <samples>  Number of samples to progress for each frame [default: 24]
```

![Example Output 1](/media/example_output/one.png?raw=true)

![Example Output 2](/media/example_output/two.png?raw=true)
