#include "Graphics.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace tannhauser {

Graphics::Graphics(uint32_t* buffer, uint32_t width, uint32_t height, int scale)
    : buffer_(buffer), logicalWidth_(width), logicalHeight_(height), scale_(scale) {
    bufferWidth_ = logicalWidth_ * scale_;
    bufferHeight_ = logicalHeight_ * scale_;
}

void Graphics::clear(uint32_t color) {
    size_t totalPixels = bufferWidth_ * bufferHeight_;
    for (size_t i = 0; i < totalPixels; ++i) {
        buffer_[i] = color;
    }
}

void Graphics::blendPixel(int bx, int by, uint32_t srcColor) {
    if (bx < 0 || bx >= static_cast<int>(bufferWidth_) || by < 0 || by >= static_cast<int>(bufferHeight_)) {
        return;
    }

    uint32_t srcA = (srcColor >> 24) & 0xFF;
    if (srcA == 0) return;

    size_t idx = static_cast<size_t>(by) * bufferWidth_ + static_cast<size_t>(bx);
    if (srcA == 255) {
        buffer_[idx] = srcColor;
        return;
    }

    uint32_t dstColor = buffer_[idx];
    uint32_t dstA = (dstColor >> 24) & 0xFF;
    uint32_t dstR = (dstColor >> 16) & 0xFF;
    uint32_t dstG = (dstColor >> 8) & 0xFF;
    uint32_t dstB = dstColor & 0xFF;

    uint32_t srcR = (srcColor >> 16) & 0xFF;
    uint32_t srcG = (srcColor >> 8) & 0xFF;
    uint32_t srcB = srcColor & 0xFF;

    uint32_t outR = (srcR * srcA + dstR * (255 - srcA)) / 255;
    uint32_t outG = (srcG * srcA + dstG * (255 - srcA)) / 255;
    uint32_t outB = (srcB * srcA + dstB * (255 - srcA)) / 255;
    uint32_t outA = std::max(srcA, dstA);

    buffer_[idx] = (outA << 24) | (outR << 16) | (outG << 8) | outB;
}

void Graphics::setPixel(int x, int y, uint32_t color) {
    if (scale_ == 1) {
        blendPixel(x, y, color);
        return;
    }
    for (int dy = 0; dy < scale_; ++dy) {
        for (int dx = 0; dx < scale_; ++dx) {
            blendPixel(x * scale_ + dx, y * scale_ + dy, color);
        }
    }
}

void Graphics::drawLine(int x0, int y0, int x1, int y1, uint32_t color, int thickness) {
    int sx0 = x0 * scale_;
    int sy0 = y0 * scale_;
    int sx1 = x1 * scale_;
    int sy1 = y1 * scale_;
    int sThick = thickness * scale_;

    int dx = std::abs(sx1 - sx0);
    int dy = std::abs(sy1 - sy0);
    int stepX = (sx0 < sx1) ? 1 : -1;
    int stepY = (sy0 < sy1) ? 1 : -1;
    int err = dx - dy;

    int halfThick = sThick / 2;

    while (true) {
        for (int ty = -halfThick; ty <= halfThick; ++ty) {
            for (int tx = -halfThick; tx <= halfThick; ++tx) {
                blendPixel(sx0 + tx, sy0 + ty, color);
            }
        }

        if (sx0 == sx1 && sy0 == sy1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            sx0 += stepX;
        }
        if (e2 < dx) {
            err += dx;
            sy0 += stepY;
        }
    }
}

void Graphics::drawRect(int x, int y, int width, int height, uint32_t color) {
    drawLine(x, y, x + width - 1, y, color);
    drawLine(x + width - 1, y, x + width - 1, y + height - 1, color);
    drawLine(x, y + height - 1, x + width - 1, y + height - 1, color);
    drawLine(x, y, x, y + height - 1, color);
}

void Graphics::fillRect(int x, int y, int width, int height, uint32_t color) {
    int sx = x * scale_;
    int sy = y * scale_;
    int sw = width * scale_;
    int sh = height * scale_;

    for (int py = sy; py < sy + sh; ++py) {
        for (int px = sx; px < sx + sw; ++px) {
            blendPixel(px, py, color);
        }
    }
}

void Graphics::drawCircle(int cx, int cy, int radius, uint32_t color, int thickness) {
    int scx = cx * scale_ + scale_ / 2;
    int scy = cy * scale_ + scale_ / 2;
    int sRadius = radius * scale_;
    int sThick = thickness * scale_;

    int rInnerSq = (sRadius - sThick / 2) * (sRadius - sThick / 2);
    int rOuterSq = (sRadius + sThick / 2 + 1) * (sRadius + sThick / 2 + 1);

    int minX = std::max(0, scx - sRadius - sThick);
    int maxX = std::min(static_cast<int>(bufferWidth_) - 1, scx + sRadius + sThick);
    int minY = std::max(0, scy - sRadius - sThick);
    int maxY = std::min(static_cast<int>(bufferHeight_) - 1, scy + sRadius + sThick);

    for (int py = minY; py <= maxY; ++py) {
        int dy = py - scy;
        for (int px = minX; px <= maxX; ++px) {
            int dx = px - scx;
            int distSq = dx * dx + dy * dy;
            if (distSq >= rInnerSq && distSq <= rOuterSq) {
                blendPixel(px, py, color);
            }
        }
    }
}

void Graphics::fillCircle(int cx, int cy, int radius, uint32_t color) {
    int scx = cx * scale_ + scale_ / 2;
    int scy = cy * scale_ + scale_ / 2;
    int sRadius = radius * scale_;
    int rSq = sRadius * sRadius;

    int minX = std::max(0, scx - sRadius);
    int maxX = std::min(static_cast<int>(bufferWidth_) - 1, scx + sRadius);
    int minY = std::max(0, scy - sRadius);
    int maxY = std::min(static_cast<int>(bufferHeight_) - 1, scy + sRadius);

    for (int py = minY; py <= maxY; ++py) {
        int dy = py - scy;
        for (int px = minX; px <= maxX; ++px) {
            int dx = px - scx;
            if (dx * dx + dy * dy <= rSq) {
                blendPixel(px, py, color);
            }
        }
    }
}

void Graphics::drawText(const Font& font, const char* text, int x, int y, uint32_t color, int textScale) {
    if (!text) return;
    if (textScale < 1) textScale = 1;

    int currX = x;
    size_t len = std::strlen(text);

    int fontW = font.getWidth();
    int fontH = font.getHeight();

    for (size_t i = 0; i < len; ++i) {
        char c = text[i];
        const uint8_t* glyph = font.getGlyph(c);

        for (int gy = 0; gy < fontH; ++gy) {
            uint8_t row = glyph[gy];
            for (int gx = 0; gx < fontW; ++gx) {
                if ((row & (1 << (7 - gx))) != 0) {
                    if (textScale == 1) {
                        setPixel(currX + gx, y + gy, color);
                    } else {
                        fillRect(currX + gx * textScale, y + gy * textScale, textScale, textScale, color);
                    }
                }
            }
        }

        currX += (fontW + 1) * textScale; // character spacing
    }
}

} // namespace tannhauser
