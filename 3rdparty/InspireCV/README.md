# InspireCV

A lightweight computer vision library with flexible backend options.

## Features

- Supports both OpenCV and custom OKCV backends
- Core functionality includes:
  - Basic image processing operations
  - Geometric primitives (Point, Rect, Size)
  - Transform matrices
  - Image I/O
- Minimal dependencies when using OKCV backend
- Optional OpenCV integration for debugging and visualization

## Build Options

### Backend Selection
- `INSPIRECV_BACKEND_OPENCV`: Use OpenCV as the backend (OFF by default)
- `INSPIRECV_BACKEND_OKCV_USE_OPENCV`: Enable OpenCV support in OKCV backend (OFF by default)
- `INSPIRECV_BACKEND_OKCV_USE_OPENCV_IO`: Use OpenCV's image I/O in OKCV (OFF by default)
- `INSPIRECV_BACKEND_OKCV_USE_OPENCV_GUI`: Use OpenCV's GUI features in OKCV (OFF by default)

### Other Options
- `INSPIRECV_BUILD_SHARED_LIBS`: Build as shared libraries (OFF by default)
- `INSPIRECV_OKCV_BUILD_TESTS`: Build test suite (ON by default)
- `INSPIRECV_OKCV_BUILD_SAMPLE`: Build sample applications (ON by default)

## Dependencies

Required:
- CMake 3.10+
- Eigen3
- C++14 compiler

Optional:
- OpenCV (required if using OpenCV backend or OpenCV features in OKCV)

## Building

To build InspireCV, simply run:

```bash
./command/build.sh
```

## Basic Examples

### Image Processing Operations

```cpp
#include <inspirecv/inspirecv.h>

using namespace inspirecv;

int main() {
    // Load and process an image
    Image img = Image::Create("input.jpg", 3);  // Load with 3 channels (RGB)
    
    // Basic operations
    Image gray = img.ToGray();                  // Convert to grayscale
    Image blurred = gray.GaussianBlur(3, 1.0);  // Apply Gaussian blur
    Image thresh = blurred.Threshold(127, 255, 0);  // Binary threshold
    
    // Geometric transformations
    Image resized = img.Resize(640, 480);       // Resize image
    Image rotated = img.Rotate90();             // Rotate 90 degrees clockwise
    Image flipped = img.FlipHorizontal();       // Flip horizontally
    
    // Save results
    resized.Write("resized.jpg");
    rotated.Write("rotated.jpg");
}
```

### Geometric Types

```cpp
#include <inspirecv/inspirecv.h>

using namespace inspirecv;

// Points
Point2f p1(100.0f, 200.0f);
Point2f p2(300.0f, 400.0f);
float distance = p1.Distance(p2);

// Rectangles
Rect2i rect(10, 20, 100, 200);  // x, y, width, height
Point2i center = rect.Center();
auto vertices = rect.ToFourVertices();

// Sizes
Size2f size(640.0f, 480.0f);
float area = size.Area();
```

### Transform Operations

```cpp
#include <inspirecv/inspirecv.h>

using namespace inspirecv;

// Create and manipulate transform matrices
TransformMatrix transform;
transform.Rotate(45.0f);         // Rotate 45 degrees
transform.Translate(10.0f, 20.0f);  // Translate
transform.Scale(2.0f, 2.0f);     // Scale
```

### Drawing Operations

```cpp
#include <inspirecv/inspirecv.h>

using namespace inspirecv;

int main() {
    Image canvas(800, 600, 3);  // Create blank RGB image
    canvas.Fill(255);           // Fill with white
    
    // Draw shapes
    canvas.DrawLine(Point2i(100, 100), Point2i(300, 300), {255, 0, 0}, 2);
    canvas.DrawRect(Rect2i(200, 200, 100, 100), {0, 255, 0}, 2);
    canvas.DrawCircle(Point2i(400, 300), 50, {0, 0, 255}, 2);
    
    canvas.Write("drawing.jpg");
}
```

## Image Features

### Image Padding and Cropping

```cpp
#include <inspirecv/inspirecv.h>

Image img = Image::Create("input.jpg");

// Add padding
Image padded = img.Pad(10, 10, 10, 10, {128, 128, 128});  // Add gray padding

// Crop region of interest
Rect2i roi(100, 100, 200, 200);
Image cropped = img.Crop(roi);
```

### Affine Transformations

```cpp
#include <inspirecv/inspirecv.h>

// Create custom affine transform
TransformMatrix transform = TransformMatrix::Create(
    0.866f, -0.5f, 100.0f,   // cos30°, -sin30°, tx
    0.5f, 0.866f, 50.0f      // sin30°, cos30°, ty
);

// Apply transform to image
Image warped = img.WarpAffine(transform, img.Width(), img.Height());
```

## Performance Considerations

- The library uses Eigen3 for efficient matrix operations
- OKCV backend provides lightweight alternatives to OpenCV
- Operations are designed to minimize memory allocations
- Thread-safe operations for parallel processing

## Error Handling

The library uses error codes and exceptions to handle error conditions:

- Image loading/saving errors
- Invalid parameters
- Memory allocation failures
- Backend-specific errors

Errors can be caught using standard try-catch blocks:

```cpp
try {
    Image img = Image::Create("nonexistent.jpg");
} catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
}
```
