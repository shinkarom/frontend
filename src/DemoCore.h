#pragma once
#include "ICore.h" // Assuming your interface is named ICore.h
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>

class DemoCore : public ICore {
private:
    CoreConfig m_config;
    bool m_romLoaded = false;

    // Video (Using Raylib's Color struct which matches R8G8B8A8)
    std::vector<Color> m_framebuffer;

    // Audio
    std::vector<int16_t> m_audioBuffer;
    double m_audioPhase = 0.0;

    // Input (Port 0 only for this demo)
    std::array<bool, 16> m_buttons{};
    std::array<float, 4> m_axes{};

    // Demo Game State
    float m_playerX = 0.0f;
    float m_playerY = 0.0f;
    uint32_t m_frameCount = 0;

public:
    DemoCore() {
        m_config.title = "Demo Core";
        m_config.targetFps = 60.0;
        m_config.vramWidth = 320;
        m_config.vramHeight = 240;
        m_config.pixelFormat = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        m_config.targetAspectRatio = 4.0f / 3.0f;
        m_config.sampleRate = 44100;
        m_config.audioChannels = 2;

        // Allocate VRAM
        m_framebuffer.resize(m_config.vramWidth * m_config.vramHeight, BLACK);
        
        // Reserve ~2 frames worth of audio samples
        m_audioBuffer.reserve((m_config.sampleRate / 30) * m_config.audioChannels);

        reset();
    }

    ~DemoCore() override = default;

    // --- Core File / Memory Management ---

    bool loadRomFile(std::string_view filePath) override {
        // For the demo, any file path is accepted as a valid "ROM"
        if (filePath.empty()) return false;
        m_romLoaded = true;
        reset();
        return true;
    }

    bool loadRomData(std::span<const uint8_t> data) override {
        if (data.empty()) return false;
        m_romLoaded = true;
        reset();
        return true;
    }

    void unloadRom() override {
        m_romLoaded = false;
        std::fill(m_framebuffer.begin(), m_framebuffer.end(), BLACK);
        m_audioBuffer.clear();
    }

    bool isRomLoaded() const override {
        return m_romLoaded;
    }

    void reset() override {
        m_playerX = m_config.vramWidth / 2.0f;
        m_playerY = m_config.vramHeight / 2.0f;
        m_frameCount = 0;
        m_audioPhase = 0.0;
        m_buttons.fill(false);
        m_axes.fill(0.0f);
        m_audioBuffer.clear();
    }

    // --- System Logic ---

    CoreConfig getConfig() const override {
        return m_config;
    }

    void stepFrame() override {
        if (!m_romLoaded) return;

        m_frameCount++;

        updateInput();
        renderGraphics();
        generateAudio();
    }

    // --- Graphics ---

    const void* getFramebuffer() const override {
        return m_framebuffer.data();
    }

    DisplayRegion getDisplayRegion() const override {
        return DisplayRegion{ 0, 0, m_config.vramWidth, m_config.vramHeight };
    }

    // --- Audio ---

    std::span<const int16_t> getAudioSamples() override {
        return m_audioBuffer;
    }

    void clearAudioSamples() override {
        m_audioBuffer.clear();
    }

    // --- Input ---

    void setButton(int port, VirtualButton btn, bool pressed) override {
        if (port != 0) return; // Demo only listens to Player 1 (Port 0)
        size_t index = static_cast<size_t>(btn);
        if (index < m_buttons.size()) {
            m_buttons[index] = pressed;
        }
    }

    void setAxis(int port, VirtualAxis axis, float value) override {
        if (port != 0) return; // Demo only listens to Player 1 (Port 0)
        size_t index = static_cast<size_t>(axis);
        if (index < m_axes.size()) {
            m_axes[index] = std::clamp(value, -1.0f, 1.0f);
        }
    }

private:
    // --- Internal Demo Helpers ---

    bool getButton(VirtualButton btn) const {
        return m_buttons[static_cast<size_t>(btn)];
    }

    float getAxis(VirtualAxis axis) const {
        return m_axes[static_cast<size_t>(axis)];
    }

    void updateInput() {
        const float speed = 2.5f;
        
        // Digital movement
        if (getButton(VirtualButton::Left))  m_playerX -= speed;
        if (getButton(VirtualButton::Right)) m_playerX += speed;
        if (getButton(VirtualButton::Up))    m_playerY -= speed;
        if (getButton(VirtualButton::Down))  m_playerY += speed;

        // Analog movement (with simple deadzone)
        if (std::abs(getAxis(VirtualAxis::LeftX)) > 0.15f) {
            m_playerX += getAxis(VirtualAxis::LeftX) * speed;
        }
        if (std::abs(getAxis(VirtualAxis::LeftY)) > 0.15f) {
            m_playerY += getAxis(VirtualAxis::LeftY) * speed;
        }

        // Clamp player to screen bounds (player size is 20x20)
        m_playerX = std::clamp(m_playerX, 10.0f, m_config.vramWidth - 10.0f);
        m_playerY = std::clamp(m_playerY, 10.0f, m_config.vramHeight - 10.0f);
    }

    void renderGraphics() {
        bool actionPressed = getButton(VirtualButton::ActionA);
        Color playerColor = actionPressed ? RED : RAYWHITE;

        int pMinX = static_cast<int>(m_playerX - 10);
        int pMaxX = static_cast<int>(m_playerX + 10);
        int pMinY = static_cast<int>(m_playerY - 10);
        int pMaxY = static_cast<int>(m_playerY + 10);

        for (int y = 0; y < m_config.vramHeight; ++y) {
            for (int x = 0; x < m_config.vramWidth; ++x) {
                int index = y * m_config.vramWidth + x;

                // Draw Player Box
                if (x >= pMinX && x < pMaxX && y >= pMinY && y < pMaxY) {
                    m_framebuffer[index] = playerColor;
                } 
                // Draw scrolling XOR background
                else {
                    uint8_t val = (x + m_frameCount) ^ (y + m_frameCount);
                    m_framebuffer[index] = Color{ 
                        static_cast<uint8_t>(val / 4), 
                        static_cast<uint8_t>(val / 2), 
                        val, 
                        255 
                    };
                }
            }
        }
    }

    void generateAudio() {
        // Calculate how many stereo frames we need for 1 video frame (44100 / 60 = 735)
        int framesPerStep = static_cast<int>(m_config.sampleRate / m_config.targetFps);
        bool playBeep = getButton(VirtualButton::ActionA);
        
        const double frequency = 440.0; // A4 Note
        const double phaseIncrement = (2.0 * 3.14159265358979323846 * frequency) / m_config.sampleRate;
        const int16_t volume = 6000;    // Keep volume reasonable (Max is 32767)

        for (int i = 0; i < framesPerStep; ++i) {
            int16_t sample = 0;
            
            if (playBeep) {
                sample = static_cast<int16_t>(std::sin(m_audioPhase) * volume);
                m_audioPhase += phaseIncrement;
                // Keep phase from overflowing
                if (m_audioPhase > 2.0 * 3.14159265358979323846) {
                    m_audioPhase -= 2.0 * 3.14159265358979323846;
                }
            } else {
                m_audioPhase = 0.0;
            }

            // Push Left and Right channels
            m_audioBuffer.push_back(sample); // Left
            m_audioBuffer.push_back(sample); // Right
        }
    }
};