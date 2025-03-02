#include <SFML/Graphics.hpp>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <atomic>

// Window settings
constexpr int WINDOW_WIDTH = 800;
constexpr int WINDOW_HEIGHT = 800;
constexpr char WINDOW_TITLE[] = "Fractal Renderer";

// Mandelbrot parameters
constexpr double ESCAPE_RADIUS_SQUARED = 100.0 * 100.0;

// Performance settings
const int NUM_THREADS = std::thread::hardware_concurrency() > 0 ? std::thread::hardware_concurrency() : 8;
constexpr int PREVIEW_DOWNSCALE = 12;
constexpr float SCROLL_RENDER_DELAY = 0.1f;
constexpr int SCREENSHOT_SCALE = 3;

// Rendering state
struct RenderState {
    double viewportX = -0.5;
    double viewportY = 0.0;
    double viewportSize = 3.0;
    int maxIterations = 128;
    float colorDensity = 0.2;
    bool showJulia = false;
    double juliaX = -0.8;
    double juliaY = 0.156;
    int colorScheme = 0;
    bool autoIterations = true;
    int fractalType = 0;
    bool stripes = true;
    float stripeFrequency = 5;
    float stripeIntensity = 10;
    bool innerCalculation = false;
};

struct ReturnInfo {
    int iteration;
    double smoothIteration;
    double stripeSum;
};

// Forward declarations
void renderFractalRegion(sf::Uint8* pixels, const RenderState& state, int startY, int endY, int width, int height);
void renderPreview(sf::Uint8* pixels, const RenderState& state, int width, int height, int downscale);
void saveScreenshot(const sf::Texture& texture, const RenderState& state);
std::string getInfoString(const RenderState& state, double mouseX, double mouseY);

// Color palettes
const std::vector<std::vector<sf::Color>> PALETTES = {
    // Classic blue-gold palette
    {
        sf::Color(66, 30, 15), sf::Color(25, 7, 26), sf::Color(9, 1, 47),
        sf::Color(4, 4, 73), sf::Color(0, 7, 100), sf::Color(12, 44, 138),
        sf::Color(24, 82, 177), sf::Color(57, 125, 209), sf::Color(134, 181, 229),
        sf::Color(211, 236, 248), sf::Color(241, 233, 191), sf::Color(248, 201, 95),
        sf::Color(255, 170, 0), sf::Color(204, 128, 0), sf::Color(153, 87, 0),
    },
    // Fire palette
    {
        sf::Color(0, 0, 0), sf::Color(20, 0, 0), sf::Color(40, 0, 0),
        sf::Color(80, 0, 0), sf::Color(120, 20, 0), sf::Color(160, 40, 0),
        sf::Color(200, 80, 0), sf::Color(240, 120, 0), sf::Color(255, 160, 0),
        sf::Color(255, 200, 0), sf::Color(255, 240, 40), sf::Color(255, 255, 100),
        sf::Color(255, 255, 170), sf::Color(255, 255, 220), sf::Color(255, 255, 255),
    },
    // Grayscale palette
    {
        sf::Color(0, 0, 0), sf::Color(32, 32, 32), sf::Color(64, 64, 64),
        sf::Color(96, 96, 96), sf::Color(128, 128, 128), sf::Color(160, 160, 160),
        sf::Color(192, 192, 192), sf::Color(224, 224, 224), sf::Color(255, 255, 255),
    },

    // Ocean depths palette
{
    sf::Color(3, 13, 30), sf::Color(6, 26, 48), sf::Color(9, 38, 67),
    sf::Color(17, 55, 92), sf::Color(25, 71, 116), sf::Color(33, 88, 140),
    sf::Color(41, 105, 165), sf::Color(50, 138, 193), sf::Color(64, 174, 224),
    sf::Color(110, 197, 233), sf::Color(158, 218, 241), sf::Color(198, 236, 248),
    sf::Color(214, 249, 255), sf::Color(225, 252, 255), sf::Color(240, 255, 255),
},

// Arctic palette
{
    sf::Color(15, 20, 40), sf::Color(20, 30, 65), sf::Color(30, 40, 90),
    sf::Color(40, 60, 120), sf::Color(65, 90, 150), sf::Color(95, 130, 180),
    sf::Color(135, 175, 205), sf::Color(175, 205, 225), sf::Color(200, 225, 240),
    sf::Color(220, 235, 245), sf::Color(230, 243, 250), sf::Color(240, 250, 253),
    sf::Color(245, 253, 255), sf::Color(250, 255, 255), sf::Color(255, 255, 255),
}
};

// Linear interpolation for colors
inline sf::Color interpolateColors(const sf::Color& c1, const sf::Color& c2, double factor) {
    return sf::Color(
        static_cast<sf::Uint8>(c1.r + factor * (c2.r - c1.r)),
        static_cast<sf::Uint8>(c1.g + factor * (c2.g - c1.g)),
        static_cast<sf::Uint8>(c1.b + factor * (c2.b - c1.b))
    );
}

// Fast calculation of fractal iteration count with optimizations
inline ReturnInfo calculateFractal(double cr, double ci, double jr, double ji, int maxIter, bool isJulia, int fractalType, bool stripes, float stripeFrequency, bool innerCalculation) {
    // Set initial values based on fractal type
    double zr = isJulia ? cr : 0;
    double zi = isJulia ? ci : 0;
    double cr_actual = isJulia ? jr : cr;
    double ci_actual = isJulia ? ji : ci;

    ReturnInfo iterationInfo;

    // Early bailout checks for Mandelbrot
    if (innerCalculation && !isJulia && fractalType == 0) {
        // Cardioid check
        double q = (cr - 0.25) * (cr - 0.25) + ci * ci;
        if (q * (q + (cr - 0.25)) < 0.25 * ci * ci) {
            iterationInfo.iteration = -1;
            return iterationInfo;
        }

        // Period-2 bulb check
        if ((cr + 1.0) * (cr + 1.0) + ci * ci < 0.0625) {
            iterationInfo.iteration = -1;
            return iterationInfo;
        }
    }

    double zr2 = zr * zr;
    double zi2 = zi * zi;
    float stripeSum = 0;
    int i = 0;

    // Main iteration loop (optimized)
    while (zr2 + zi2 < ESCAPE_RADIUS_SQUARED) {
        zi = (fractalType == 0) ? 2 * zr * zi : 2 * fabs(zr * zi);
        zi += ci_actual;
        zr = zr2 - zi2 + cr_actual;
        zr2 = zr * zr;
        zi2 = zi * zi;
        if (stripes) stripeSum += powf(sin(atan2(zi, zr) * stripeFrequency), 2.0);
        i++;
        if (i == maxIter) {
            if (innerCalculation) {
                iterationInfo.iteration = i;
                iterationInfo.smoothIteration = i + 1 - log(log(zr2 + zi2) / 2) / log(2);
                iterationInfo.stripeSum = stripeSum;
                return iterationInfo;
            }
            else {
                iterationInfo.iteration = -1;
                return iterationInfo;
            }
        }
    }

    // Smooth coloring formula
    iterationInfo.iteration = i;
    iterationInfo.smoothIteration = i + 1 - log(log(zr2 + zi2) / 2) / log(2);
    iterationInfo.stripeSum = stripeSum;
    return iterationInfo;
}

// Render the fractal using multiple threads
void renderFractal(sf::Uint8* pixels, const RenderState& state, int width, int height, bool usePreview = false) {
    if (usePreview) {
        renderPreview(pixels, state, width, height, PREVIEW_DOWNSCALE);
    }
    else {
        std::vector<std::thread> threads;
        int linesPerThread = height / NUM_THREADS;

        for (int i = 0; i < NUM_THREADS; i++) {
            int startY = i * linesPerThread;
            int endY = (i == NUM_THREADS - 1) ? height : (i + 1) * linesPerThread;
            threads.emplace_back(renderFractalRegion, pixels, std::ref(state), startY, endY, width, height);
        }

        for (auto& thread : threads) {
            thread.join();
        }
    }
}

// Render a region of the fractal (for multi-threading)
void renderFractalRegion(sf::Uint8* pixels, const RenderState& state, int startY, int endY, int width, int height) {
    double pixelSize = state.viewportSize / width;
    double halfSize = state.viewportSize / 2;
    const auto& palette = PALETTES[state.colorScheme % PALETTES.size()];

    for (int y = startY; y < endY; y++) {
        double ci = state.viewportY - halfSize + y * pixelSize;

        for (int x = 0; x < width; x++) {
            double cr = state.viewportX - halfSize + x * pixelSize;

            // Calculate iterations
            ReturnInfo info = calculateFractal(cr, ci, state.juliaX, state.juliaY,
                state.maxIterations, state.showJulia, state.fractalType, state.stripes, state.stripeFrequency, state.innerCalculation);

            sf::Color color;
            if (info.iteration == -1) {
                color = sf::Color(0, 0, 0);
            }
            else {
                float iterations;
                if (state.stripes)
                {
                    iterations = state.stripeIntensity * (info.stripeSum / info.iteration);
                }
                else {
                    iterations = info.smoothIteration * state.colorDensity;
                }
                int index = static_cast<int>(iterations) % palette.size();
                double fract = iterations - std::floor(iterations);
                color = interpolateColors(palette[index], palette[(index + 1) % palette.size()], fract);
            }

            int pixelIndex = (y * width + x) * 4;
            pixels[pixelIndex] = color.r;
            pixels[pixelIndex + 1] = color.g;
            pixels[pixelIndex + 2] = color.b;
            pixels[pixelIndex + 3] = 255;
        }
    }
}

// Render low-resolution preview for faster interaction
void renderPreview(sf::Uint8* pixels, const RenderState& state, int width, int height, int downscale) {
    double pixelSize = state.viewportSize / width;
    double halfSize = state.viewportSize / 2;
    const auto& palette = PALETTES[state.colorScheme % PALETTES.size()];

    for (int y = 0; y < height; y += downscale) {
        double ci = state.viewportY - halfSize + y * pixelSize;

        for (int x = 0; x < width; x += downscale) {
            double cr = state.viewportX - halfSize + x * pixelSize;

            // Calculate iterations
            ReturnInfo info = calculateFractal(cr, ci, state.juliaX, state.juliaY,
                state.maxIterations, state.showJulia, state.fractalType, state.stripes, state.stripeFrequency, state.innerCalculation);

            sf::Color color;
            if (info.iteration == -1) {
                color = sf::Color(0, 0, 0);
            }
            else {
                float iterations;
                if (state.stripes)
                {
                    iterations = state.stripeIntensity * (info.stripeSum / info.iteration);
                }
                else {
                    iterations = info.smoothIteration * state.colorDensity;
                }
                int index = static_cast<int>(iterations) % palette.size();
                double fract = iterations - std::floor(iterations);
                color = interpolateColors(palette[index], palette[(index + 1) % palette.size()], fract);
            }


            // Fill block with the same color
            for (int by = 0; by < downscale && y + by < height; by++) {
                for (int bx = 0; bx < downscale && x + bx < width; bx++) {
                    int idx = ((y + by) * width + (x + bx)) * 4;
                    pixels[idx] = color.r;
                    pixels[idx + 1] = color.g;
                    pixels[idx + 2] = color.b;
                    pixels[idx + 3] = 255;
                }
            }
        }
    }
}

// Save screenshot with location info in filename
void saveScreenshot(const sf::Texture& texture, const RenderState& state) {
    sf::Image screenshot = texture.copyToImage();

    time_t now = time(0);
    tm timeinfo;

#ifdef _WIN32
    localtime_s(&timeinfo, &now);
#else
    tm* temp = localtime(&now);
    if (temp) timeinfo = *temp;
#endif

    std::stringstream filename;
    filename << "fractal_" << (state.showJulia ? "julia_" : "mandelbrot_")
        << std::fixed << std::setprecision(6) << state.viewportX << "_" << state.viewportY
        << "_zoom_" << std::setprecision(2) << (3.0 / state.viewportSize)
        << now << ".png";

    screenshot.saveToFile(filename.str());
    std::cout << "Screenshot saved: " << filename.str() << std::endl;
}

// Add this function to create and save high resolution screenshots
void saveHighResScreenshot(const RenderState& state, int width, int height, int scale) {
    // Create high-resolution pixel buffer
    int hiResWidth = width * scale;
    int hiResHeight = height * scale;
    sf::Uint8* hiResPixels = new sf::Uint8[hiResWidth * hiResHeight * 4];

    std::cout << "Rendering high-resolution screenshot (" << hiResWidth << "x" << hiResHeight << ")..." << std::endl;

    // Render the fractal at high resolution
    RenderState hiResState = state;
    // Adjust pixel density without changing view dimensions
    hiResState.viewportSize = state.viewportSize;

    // Use multi-threading to render high-res image
    std::vector<std::thread> threads;
    int linesPerThread = hiResHeight / NUM_THREADS;

    for (int i = 0; i < NUM_THREADS; i++) {
        int startY = i * linesPerThread;
        int endY = (i == NUM_THREADS - 1) ? hiResHeight : (i + 1) * linesPerThread;
        threads.emplace_back(renderFractalRegion, hiResPixels, std::ref(hiResState), startY, endY, hiResWidth, hiResHeight);
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Create image and save it
    sf::Image screenshot;
    screenshot.create(hiResWidth, hiResHeight, hiResPixels);

    time_t now = time(0);
    tm timeinfo;

#ifdef _WIN32
    localtime_s(&timeinfo, &now);
#else
    tm* temp = localtime(&now);
    if (temp) timeinfo = *temp;
#endif

    std::stringstream filename;
    filename << "fractal_" << (state.showJulia ? "julia_" : "mandelbrot_")
        << std::fixed << std::setprecision(6) << state.viewportX << "_" << state.viewportY
        << "_zoom_" << std::setprecision(2) << (3.0 / state.viewportSize)
        << "_hires_" << hiResWidth << "x" << hiResHeight << "_"
        << now << ".png";

    screenshot.saveToFile(filename.str());
    std::cout << "High-resolution screenshot saved: " << filename.str() << std::endl;

    // Free memory
    delete[] hiResPixels;
}

// Get formatted info string
std::string getInfoString(const RenderState& state, double mouseX, double mouseY) {
    std::stringstream ss;
    ss << "Mode: " << (state.showJulia ? "Julia" : "Mandelbrot") << "\n";
    ss << "Position: (" << std::fixed << std::setprecision(10) << state.viewportX
        << ", " << state.viewportY << ")\n";
    ss << "Zoom: " << std::setprecision(2) << (3.0 / state.viewportSize) << "x\n";
    ss << "Iterations: " << state.maxIterations << (state.autoIterations ? " (auto)" : "") << "\n";

    if (state.showJulia) {
        ss << "Julia seed: (" << std::setprecision(6) << state.juliaX << ", " << state.juliaY << ")\n";
    }

    ss << "Color scheme: " << state.colorScheme + 1 << "/" << PALETTES.size() << "\n";
    ss << "Mouse: (" << std::setprecision(6) << mouseX << ", " << mouseY << ")\n\n";
    ss << "Controls: Scroll=Zoom, Drag=Pan, J=Julia/Mandelbrot, C=Colors,\n";
    ss << "R=Reset, S=Screenshot, I/K=Iterations, A=Auto iterations";

    return ss.str();
}

// Auto-adjust iterations based on zoom level
void adjustIterations(RenderState& state) {
    if (state.autoIterations) {
        double zoomFactor = 3.0 / state.viewportSize;
        state.maxIterations = std::min(10000, static_cast<int>(100 * log10(1 + zoomFactor)));
        state.maxIterations = std::max(100, state.maxIterations);
    }
}

int main() {
    std::cout << "Starting Fractal Explorer with " << NUM_THREADS << " threads" << std::endl;

    // Create window and rendering resources
    sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), WINDOW_TITLE);
    window.setFramerateLimit(60);

    sf::Texture texture;
    if (!texture.create(WINDOW_WIDTH, WINDOW_HEIGHT)) {
        std::cerr << "Failed to create texture" << std::endl;
        return 1;
    }

    sf::Sprite sprite(texture);
    sf::Uint8* pixels = new sf::Uint8[WINDOW_WIDTH * WINDOW_HEIGHT * 4];

    // Load font for text display
    sf::Font font;
    bool hasFontLoaded = font.loadFromFile("arial.ttf");
    if (!hasFontLoaded) {
        // Try system fonts as fallback
#ifdef _WIN32
        hasFontLoaded = font.loadFromFile("C:\\Windows\\Fonts\\arial.ttf");
#elif __APPLE__
        hasFontLoaded = font.loadFromFile("/System/Library/Fonts/Helvetica.ttc");
#elif __linux__
        hasFontLoaded = font.loadFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
#endif
    }

    sf::Text infoText;
    sf::Text performanceText;

    if (hasFontLoaded) {
        infoText.setFont(font);
        infoText.setCharacterSize(12);
        infoText.setFillColor(sf::Color::White);
        infoText.setOutlineColor(sf::Color::Black);
        infoText.setOutlineThickness(1);
        infoText.setPosition(10, 10);

        performanceText.setFont(font);
        performanceText.setCharacterSize(14);
        performanceText.setFillColor(sf::Color::Yellow);
        performanceText.setOutlineColor(sf::Color::Black);
        performanceText.setOutlineThickness(1);
        performanceText.setPosition(10, WINDOW_HEIGHT - 30);
    }

    // Initialize state and render
    RenderState state;
    adjustIterations(state);

    auto startTime = std::chrono::high_resolution_clock::now();
    renderFractal(pixels, state, WINDOW_WIDTH, WINDOW_HEIGHT);
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    std::cout << "Initial render: " << duration << "ms" << std::endl;
    texture.update(pixels);

    // Tracking variables
    sf::Vector2i lastMousePos;
    bool isDragging = false;
    std::string renderTimeStr = "Render time: " + std::to_string(duration) + "ms";
    sf::Vector2i currentMousePos;
    double mouseComplexX = 0, mouseComplexY = 0;

    // High-quality render control
    bool viewChanged = false;
    sf::Clock scrollTimer;
    bool pendingHighQualityRender = false;

    // Main loop
    while (window.isOpen()) {
        sf::Event event;
        bool needsRedraw = false;
        bool usePreview = false;

        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();

            // Zoom with mouse wheel
            if (event.type == sf::Event::MouseWheelScrolled) {
                sf::Vector2i mousePos = sf::Mouse::getPosition(window);

                if (mousePos.x >= 0 && mousePos.x < WINDOW_WIDTH &&
                    mousePos.y >= 0 && mousePos.y < WINDOW_HEIGHT) {

                    double halfSize = state.viewportSize / 2;
                    double mouseX = state.viewportX - halfSize + mousePos.x * state.viewportSize / WINDOW_WIDTH;
                    double mouseY = state.viewportY - halfSize + mousePos.y * state.viewportSize / WINDOW_HEIGHT;

                    double zoomFactor = (event.mouseWheelScroll.delta > 0) ? 0.5 : 2.0;

                    state.viewportX = mouseX + (state.viewportX - mouseX) * zoomFactor;
                    state.viewportY = mouseY + (state.viewportY - mouseY) * zoomFactor;
                    state.viewportSize *= zoomFactor;

                    adjustIterations(state);

                    needsRedraw = true;
                    usePreview = true;
                    viewChanged = true;
                    scrollTimer.restart();
                }
            }

            // Handle mouse button events for dragging
            if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
                isDragging = true;
                usePreview = true;
                lastMousePos = sf::Mouse::getPosition(window);
            }

            if (event.type == sf::Event::MouseButtonReleased && event.mouseButton.button == sf::Mouse::Left) {
                isDragging = false;
                pendingHighQualityRender = true;
                scrollTimer.restart();
            }

            // Handle mouse movement for panning and tracking
            if (event.type == sf::Event::MouseMoved) {
                currentMousePos = sf::Mouse::getPosition(window);

                double halfSize = state.viewportSize / 2;
                mouseComplexX = state.viewportX - halfSize +
                    currentMousePos.x * state.viewportSize / WINDOW_WIDTH;
                mouseComplexY = state.viewportY - halfSize +
                    currentMousePos.y * state.viewportSize / WINDOW_HEIGHT;

                if (isDragging) {
                    sf::Vector2i delta = lastMousePos - currentMousePos;

                    double deltaX = delta.x * state.viewportSize / WINDOW_WIDTH;
                    double deltaY = delta.y * state.viewportSize / WINDOW_HEIGHT;

                    state.viewportX += deltaX;
                    state.viewportY += deltaY;

                    lastMousePos = currentMousePos;
                    needsRedraw = true;
                    usePreview = true;
                    viewChanged = true;
                    scrollTimer.restart();
                }
            }

            // Key press events
            if (event.type == sf::Event::KeyPressed) {
                switch (event.key.code) {
                case sf::Keyboard::R: // Reset view
                    state.viewportX = -0.5;
                    state.viewportY = 0.0;
                    state.viewportSize = 3.0;
                    adjustIterations(state);
                    needsRedraw = true;
                    viewChanged = false;
                    pendingHighQualityRender = false;
                    break;

                case sf::Keyboard::J: // Toggle Julia/Mandelbrot
                    if (!state.showJulia) {
                        state.juliaX = mouseComplexX;
                        state.juliaY = mouseComplexY;
                    }
                    state.showJulia = !state.showJulia;
                    needsRedraw = true;
                    viewChanged = false;
                    pendingHighQualityRender = false;
                    break;

                case sf::Keyboard::C: // Change color scheme
                    state.colorScheme = (state.colorScheme + 1) % PALETTES.size();
                    needsRedraw = true;
                    viewChanged = false;
                    pendingHighQualityRender = false;
                    break;

                case sf::Keyboard::S: // Save screenshot
                    saveScreenshot(texture, state);
                    break;

                case sf::Keyboard::I: // Increase iterations
                    state.autoIterations = false;
                    state.maxIterations = std::min(10000, state.maxIterations * 2);
                    needsRedraw = true;
                    viewChanged = false;
                    pendingHighQualityRender = false;
                    break;

                case sf::Keyboard::K: // Decrease iterations
                    state.autoIterations = false;
                    state.maxIterations = std::max(100, state.maxIterations / 2);
                    needsRedraw = true;
                    viewChanged = false;
                    pendingHighQualityRender = false;
                    break;

                case sf::Keyboard::A: // Toggle auto iterations
                    state.autoIterations = !state.autoIterations;
                    if (state.autoIterations) {
                        adjustIterations(state);
                        needsRedraw = true;
                        viewChanged = false;
                        pendingHighQualityRender = false;
                    }
                    break;

                case sf::Keyboard::Up: // Adjust color density
                    state.colorDensity *= 1.1;
                    needsRedraw = true;
                    viewChanged = false;
                    pendingHighQualityRender = false;
                    break;

                case sf::Keyboard::Down: // Adjust color density
                    state.colorDensity /= 1.1;
                    needsRedraw = true;
                    viewChanged = false;
                    pendingHighQualityRender = false;
                    break;

                case sf::Keyboard::T: // Change fractal type
                    state.fractalType = (state.fractalType + 1) % 2;
                    needsRedraw = true;
                    viewChanged = false;
                    pendingHighQualityRender = false;
                    break;

                case sf::Keyboard::Z: // Change stripes type
                    state.stripes = !state.stripes;
                    needsRedraw = true;
                    viewChanged = false;
                    pendingHighQualityRender = false;
                    break;

                case sf::Keyboard::M: // Change stripes type
                    state.stripeFrequency += 0.1;
                    needsRedraw = true;
                    viewChanged = false;
                    pendingHighQualityRender = false;
                    break;

                case sf::Keyboard::N: // Change stripes type
                    state.stripeFrequency -= 0.1;
                    needsRedraw = true;
                    viewChanged = false;
                    pendingHighQualityRender = false;
                    break;

                case sf::Keyboard::V: // Change stripes type
                    state.stripeIntensity += 1;
                    needsRedraw = true;
                    viewChanged = false;
                    pendingHighQualityRender = false;
                    break;

                case sf::Keyboard::B: // 
                    state.stripeIntensity -= 1;
                    needsRedraw = true;
                    viewChanged = false;
                    pendingHighQualityRender = false;
                    break;

                case sf::Keyboard::H: // High-resolution screenshot (H key)
                    saveHighResScreenshot(state, WINDOW_WIDTH, WINDOW_HEIGHT, SCREENSHOT_SCALE);
                    break;
                }

            }
        }

        // Check if we need a high-quality render after scrolling stopped
        if ((viewChanged || pendingHighQualityRender) && !isDragging &&
            scrollTimer.getElapsedTime().asSeconds() > SCROLL_RENDER_DELAY) {
            needsRedraw = true;
            usePreview = false;
            viewChanged = false;
            pendingHighQualityRender = false;

            renderTimeStr = "Rendering high quality...";
            if (hasFontLoaded) {
                performanceText.setString(renderTimeStr);
                window.draw(performanceText);
                window.display();
            }
        }

        // Perform rendering if needed
        if (needsRedraw) {
            auto startTime = std::chrono::high_resolution_clock::now();
            renderFractal(pixels, state, WINDOW_WIDTH, WINDOW_HEIGHT, usePreview);
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

            renderTimeStr = "Render time: " + std::to_string(duration) + "ms";
            if (!usePreview) {
                renderTimeStr += " (high quality)";
            }

            texture.update(pixels);
        }

        // Update info text
        if (hasFontLoaded) {
            infoText.setString(getInfoString(state, mouseComplexX, mouseComplexY));
            performanceText.setString(renderTimeStr);
        }

        // Draw everything
        window.clear();
        window.draw(sprite);
        if (hasFontLoaded) {
            window.draw(infoText);
            window.draw(performanceText);
        }
        window.display();
    }

    delete[] pixels;
    return 0;
}
