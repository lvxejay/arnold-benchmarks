# Arnold Benchmarks

Various Arnold Renderer benchmarks and tests

## Installation

Simply copy and paste the benchmark folder somewhere convenient on your hard drive.

To allow Arnold to find the provided plugins, set kick's plugin load path with `kick -l arnold_benchmarks/lib` or set `$ARNOLD_PLUGIN/PATHS` to point to `arnold_benchmarks/lib`

## Benchmarks

1. [Antonio Bosi CPU Benchmarks](https://www.antoniobosi.com/maya-3d-models-downloads-books-guides-reviews-advices/arnold-render-cpu-speed-benchmark)

    ```
    data/bosi_bench
    ```

2. [Mandelulb - Solid Angle](https://docs.arnoldrenderer.com/display/A5AFMUG/How+to+Render+a+Mandelbulb)

    ```
    data/mandelbulb
    ```

## Usage

*Note: You must be in the root of the repo before running these commands.*

```
# Render an ass file using kick:
cd /path/to/arnold_benchmark
/opt/solidangle/Arnold-5.3.0.2/bin/kick \
-dp -dw -v 6 \
-i ./data/mandelbulb/mandelbulb.ass
```


## Advanced

### Compiling the shared libraries

The sources for the precompiled binaries are included in this repo.
In order to recompile them, make sure to provide the appropriate include directories and link paths for the Arnold SDK

```
g++ -shared -pthread -fPIC -O2 -Wall \
-I/opt/solidangle/Arnold-5.3.0.2/include \
-L/opt/solidangle/Arnold-5.3.0.2/bin -lai \
-o mandelbulb.so mandelbulb.cpp
```