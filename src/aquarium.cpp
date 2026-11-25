#include "aquarium.h"
#include <cmath>
#include <algorithm>
#include <chrono>

namespace {
constexpr float PI = 3.14159265358979323846f;

float Clamp(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi ? hi : v);
}
}

Aquarium::Aquarium() {
    auto seed = static_cast<unsigned int>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    rng_.seed(seed);
}

float Aquarium::RandFloat(float a, float b) {
    std::uniform_real_distribution<float> dist(a, b);
    return dist(rng_);
}

int Aquarium::RandInt(int a, int b) {
    if (b < a) std::swap(a, b);
    std::uniform_int_distribution<int> dist(a, b);
    return dist(rng_);
}

void Aquarium::SetTheme(int themeIdx) {
    if (themeIdx < 0 || themeIdx >= THEME_COUNT) themeIdx = 0;
    themeIndex_ = themeIdx;
}

void Aquarium::Resize(int cols, int rows, int bottomMarginRows) {
    if (cols < 40) cols = 40;
    if (rows < 18) rows = 18;
    bottomMarginRows = std::max(0, std::min(rows / 4, bottomMarginRows));
    if (cols == cols_ && rows == rows_ && bottomMarginRows == bottomMarginRows_) return;

    cols_ = cols;
    rows_ = rows;
    bottomMarginRows_ = bottomMarginRows;
    grid_.assign(static_cast<size_t>(cols_) * static_cast<size_t>(rows_), Cell{});
    Populate();
}

int Aquarium::FloorYAt(int x) const {
    int vb = VisibleBottom();
    if (floorHeights_.empty()) return vb - 3;
    int ix = (x < 0) ? 0 : (x >= cols_ ? cols_ - 1 : x);
    return floorHeights_[ix];
}

void Aquarium::Populate() {
    const int vb = VisibleBottom();
    floorHeights_.resize(cols_);
    sandNoise_.resize(static_cast<size_t>(cols_) * 8);

    // Gently undulating seabed placed cleanly ABOVE the Windows taskbar
    float p1 = RandFloat(0.0f, 6.28f);
    float p2 = RandFloat(0.0f, 6.28f);
    for (int x = 0; x < cols_; ++x) {
        float h = 2.8f
            + std::sin(x * 0.048f + p1) * 0.9f
            + std::sin(x * 0.12f + p2) * 0.45f;
        int floorY = vb - 1 - static_cast<int>(std::round(h));
        floorHeights_[x] = std::max(vb - 5, std::min(vb - 2, floorY));
    }

    const wchar_t sandChars[] = { L'.', L',', L' ', L' ', L'~', L'_', L'\x00B7', L' ' };
    for (size_t i = 0; i < sandNoise_.size(); ++i) {
        sandNoise_[i] = sandChars[RandInt(0, 7)];
    }

    // Treasure chest resting on the right-center seabed
    chest_.x = std::max(18, static_cast<int>(cols_ * 0.72f));
    chest_.openTimer = 0.0f;
    chest_.nextOpenIn = RandFloat(5.0f, 11.0f);

    // Detailed shaded rocks
    rocks_.clear();
    int rockCount = std::max(3, cols_ / 38);
    for (int i = 0; i < rockCount; ++i) {
        Rock r;
        r.width = RandInt(9, 14);
        r.height = RandInt(3, 4);
        int candidate = RandInt(6, std::max(10, cols_ - r.width - 6));
        if (std::abs(candidate - chest_.x) < 16) {
            candidate = std::max(6, (candidate + 28) % std::max(16, cols_ - r.width - 6));
        }
        r.x = candidate;
        rocks_.push_back(r);
    }

    // Detailed multi-row corals
    corals_.clear();
    int coralCount = std::max(5, cols_ / 24);
    for (int i = 0; i < coralCount; ++i) {
        Coral c;
        c.x = RandInt(6, std::max(10, cols_ - 8));
        if (std::abs(c.x - chest_.x) < 12) continue;
        c.height = RandInt(3, 5);
        c.style = i % 3;
        c.colorSlot = i % 3;
        corals_.push_back(c);
    }

    // Shells & starfish scattered on the sand
    shells_.clear();
    int shellCount = std::max(8, cols_ / 18);
    for (int i = 0; i < shellCount; ++i) {
        ShellDecor sd{};
        sd.x = RandInt(4, std::max(6, cols_ - 5));
        sd.dy = RandInt(0, 2);
        sd.star = (i % 3 == 0);
        sd.ch = sd.star ? L'*' : ((i % 2 == 0) ? L'@' : L'o');
        shells_.push_back(sd);
    }

    // Lush swaying seaweeds
    seaweeds_.clear();
    int weedCount = std::max(12, cols_ / 9);
    for (int i = 0; i < weedCount; ++i) {
        Seaweed s{};
        float slot = (static_cast<float>(i) + RandFloat(0.15f, 0.85f)) / static_cast<float>(weedCount);
        s.baseX = static_cast<int>(slot * (cols_ - 6)) + 3;
        if (std::abs(s.baseX - (chest_.x + 6)) < 8) continue;
        int maxH = std::max(6, (vb - 8) / 3);
        s.height = RandInt(std::max(5, maxH / 2), maxH);
        s.phase = RandFloat(0.0f, 2.0f * PI);
        s.speed = RandFloat(1.0f, 1.9f);
        s.bend = 0.0f;
        s.bright = (i % 2 == 0);
        s.leafy = (i % 3 != 0);
        seaweeds_.push_back(s);
    }

    // Fishes: emphasis on sleek, lively small & medium ASCII fish + a few crisp medium-large silhouettes
    fishes_.clear();
    int minnowCount   = std::max(12, cols_ / 13);
    int tropicalCount = std::max(8, cols_ / 20);
    int clownCount    = std::max(4, cols_ / 40);
    int pufferCount   = 2;
    int angelCount    = 3;
    int reefBigCount  = 2;

    int depthBand = 0;
    auto addFish = [&](FishSpecies sp) {
        Fish f{};
        f.species = sp;
        f.x = RandFloat(6.0f, static_cast<float>(cols_ - 10));
        int minY = 5;
        int maxY = std::max(minY + 4, vb - 10);
        float bandFrac = (static_cast<float>(depthBand % 3) + RandFloat(0.1f, 0.9f)) / 3.0f;
        depthBand++;
        f.y = minY + bandFrac * static_cast<float>(maxY - minY);
        f.facing = (RandInt(0, 1) == 0) ? 1 : -1;

        switch (sp) {
            case FishSpecies::Minnow:
                f.baseSpeed = RandFloat(9.0f, 14.0f);
                f.colorIndex = RandInt(0, 5);
                f.variant = RandInt(0, 2);
                break;
            case FishSpecies::Clownfish:
                f.baseSpeed = RandFloat(6.5f, 9.5f);
                f.colorIndex = 0; // clownfish orange
                f.variant = 0;
                break;
            case FishSpecies::Tropical:
                f.baseSpeed = RandFloat(7.0f, 10.5f);
                f.colorIndex = RandInt(0, 4);
                f.variant = RandInt(0, 2);
                break;
            case FishSpecies::Puffer:
                f.baseSpeed = RandFloat(4.8f, 6.8f);
                f.colorIndex = 2;
                f.variant = 0;
                break;
            case FishSpecies::Angel:
                f.baseSpeed = RandFloat(4.6f, 6.6f);
                f.colorIndex = RandInt(1, 3);
                f.variant = 0;
                break;
            case FishSpecies::ReefBig:
                f.baseSpeed = RandFloat(4.2f, 6.0f);
                f.colorIndex = RandInt(0, 2);
                f.variant = 0;
                break;
        }
        f.vx = f.baseSpeed * f.facing;
        f.vy = RandFloat(-0.5f, 0.5f);
        f.phase = RandFloat(0.0f, 2.0f * PI);
        f.scaredTimer = 0.0f;
        f.hungryCooldown = 0.0f;
        f.swimTargetY = f.y;
        f.turnTimer = RandFloat(4.5f, 13.0f);
        fishes_.push_back(f);
    };

    for (int i = 0; i < minnowCount; ++i)   addFish(FishSpecies::Minnow);
    for (int i = 0; i < clownCount; ++i)    addFish(FishSpecies::Clownfish);
    for (int i = 0; i < tropicalCount; ++i) addFish(FishSpecies::Tropical);
    for (int i = 0; i < pufferCount; ++i)   addFish(FishSpecies::Puffer);
    for (int i = 0; i < angelCount; ++i)    addFish(FishSpecies::Angel);
    for (int i = 0; i < reefBigCount; ++i)  addFish(FishSpecies::ReefBig);

    // Majestic slow-gliding Sea Turtle in the upper-middle water
    turtle_.x = cols_ * 0.28f;
    turtle_.y = static_cast<float>(std::max(7, vb / 4));
    turtle_.facing = 1;
    turtle_.vx = 3.2f;
    turtle_.vy = 0.0f;
    turtle_.phase = 0.0f;

    // Bobbing Seahorses near seaweed
    seahorses_.clear();
    int shCount = (cols_ >= 120) ? 2 : 1;
    for (int i = 0; i < shCount; ++i) {
        Seahorse sh{};
        sh.baseX = cols_ * (i == 0 ? 0.14f : 0.86f);
        sh.baseY = static_cast<float>(vb - 11 - i * 2);
        sh.x = sh.baseX;
        sh.y = sh.baseY;
        sh.phase = RandFloat(0.0f, 2.0f * PI);
        sh.facing = (i == 0) ? 1 : -1;
        sh.colorIndex = (i == 0) ? 2 : 3;
        seahorses_.push_back(sh);
    }

    // Detailed Jellyfish
    jellies_.clear();
    int jellyCount = (cols_ >= 120) ? 3 : 2;
    for (int i = 0; i < jellyCount; ++i) {
        Jellyfish j{};
        j.x = cols_ * (0.18f + i * 0.31f) + RandFloat(-5.0f, 5.0f);
        j.y = RandFloat(6.0f, static_cast<float>(std::max(12, vb - 15)));
        j.vx = RandFloat(-1.1f, 1.1f);
        j.vy = 0.0f;
        j.phase = RandFloat(0.0f, 2.0f * PI);
        j.pulseSpeed = RandFloat(1.2f, 1.7f);
        jellies_.push_back(j);
    }

    // Detailed 3-row Crab on the sand floor
    crab_.x = static_cast<float>(cols_) * 0.42f;
    crab_.vx = 3.0f;
    crab_.pauseTimer = 1.5f;
    crab_.animPhase = 0.0f;
    crab_.startledTimer = 0.0f;

    bubbles_.clear();
    for (int i = 0; i < 14; ++i) {
        SpawnBubble(RandFloat(3.0f, static_cast<float>(cols_ - 4)),
                    RandFloat(5.0f, static_cast<float>(vb - 4)));
    }
    foods_.clear();
    ripples_.clear();
    sparkles_.clear();
}

void Aquarium::SpawnBubble(float x, float y, int size, bool golden) {
    if (bubbles_.size() > 95) return;
    Bubble b{};
    b.x = x;
    b.y = y;
    b.size = (size >= 0) ? size : RandInt(0, 3);
    b.vy = -RandFloat(3.6f, 6.8f) - b.size * 0.55f;
    b.phase = RandFloat(0.0f, 2.0f * PI);
    b.popping = false;
    b.popTimer = 0.0f;
    b.golden = golden;
    bubbles_.push_back(b);
}

void Aquarium::OnMouseMove(float col, float row, bool active) {
    mouseActive_ = active;
    if (!active) {
        mouseVx_ *= 0.8f;
        mouseVy_ *= 0.8f;
        mouseSpeed_ *= 0.8f;
        return;
    }
    if (mouseX_ < -50.0f) {
        mouseX_ = col;
        mouseY_ = row;
        mouseVx_ = 0.0f;
        mouseVy_ = 0.0f;
        mouseSpeed_ = 0.0f;
        return;
    }
    float dx = col - mouseX_;
    float dy = row - mouseY_;
    mouseVx_ = mouseVx_ * 0.5f + dx * 0.5f;
    mouseVy_ = mouseVy_ * 0.5f + dy * 0.5f;
    mouseSpeed_ = std::sqrt(mouseVx_ * mouseVx_ + mouseVy_ * mouseVy_);
    mouseX_ = col;
    mouseY_ = row;

    int vb = VisibleBottom();
    if (mouseSpeed_ > 3.2f && row > 3.0f && row < vb - 3.0f && RandInt(0, 3) == 0) {
        SpawnBubble(col + RandFloat(-1.0f, 1.0f), row + RandFloat(-0.5f, 0.5f), RandInt(0, 1));
    }
}

void Aquarium::DropFood(float col, float row, int count) {
    int vb = VisibleBottom();
    float cx = Clamp(col, 2.0f, static_cast<float>(cols_ - 3));
    float cy = Clamp(row, 3.0f, static_cast<float>(vb - 6));

    Ripple rp{};
    rp.x = cx;
    rp.y = cy;
    rp.radius = 0.9f;
    rp.maxRadius = 7.0f;
    rp.life = 1.0f;
    ripples_.push_back(rp);

    const wchar_t pellets[] = { L'\x2022', L'\x00B7', L'\x00B0', L'*' };
    for (int i = 0; i < count; ++i) {
        if (foods_.size() >= 65) foods_.erase(foods_.begin());
        FoodPellet fp{};
        fp.x = Clamp(cx + RandFloat(-2.8f, 2.8f), 2.0f, static_cast<float>(cols_ - 3));
        fp.y = Clamp(cy + RandFloat(-0.8f, 0.8f), 3.0f, static_cast<float>(vb - 5));
        fp.vy = RandFloat(1.9f, 3.3f);
        fp.phase = RandFloat(0.0f, 2.0f * PI);
        fp.life = RandFloat(18.0f, 28.0f);
        fp.ch = pellets[RandInt(0, 3)];
        foods_.push_back(fp);
    }

    for (int i = 0; i < 3; ++i) {
        SpawnBubble(cx + RandFloat(-1.5f, 1.5f), cy, RandInt(0, 2));
    }
}

void Aquarium::DropFoodFromSurface(int count) {
    float cx = RandFloat(cols_ * 0.2f, cols_ * 0.8f);
    DropFood(cx, 3.5f, count);
}

void Aquarium::Update(float dt) {
    if (cols_ <= 0 || rows_ <= 0) return;
    dt = Clamp(dt, 0.001f, 0.1f);
    time_ += dt;

    UpdateSeaweed(dt);
    UpdateFood(dt);
    UpdateFish(dt);
    UpdateTurtle(dt);
    UpdateSeahorses(dt);
    UpdateJellyfish(dt);
    UpdateCrab(dt);
    UpdateChest(dt);
    UpdateBubbles(dt);
    UpdateRipples(dt);
    UpdateSparkles(dt);

    mouseVx_ *= 0.88f;
    mouseVy_ *= 0.88f;
    mouseSpeed_ *= 0.88f;
}

void Aquarium::UpdateSeaweed(float dt) {
    for (auto& s : seaweeds_) {
        s.bend += (0.0f - s.bend) * std::min(1.0f, dt * 3.5f);

        if (mouseActive_) {
            int baseY = FloorYAt(s.baseX);
            int topY = baseY - s.height;
            float dx = static_cast<float>(s.baseX) - mouseX_;
            if (std::fabs(dx) < 10.0f && mouseY_ >= topY - 2 && mouseY_ <= baseY + 1) {
                float push = (mouseSpeed_ > 0.5f) ? Clamp(mouseVx_ * 0.45f, -3.5f, 3.5f)
                                                  : ((dx >= 0.0f) ? 1.2f : -1.2f);
                s.bend = Clamp(s.bend + push * dt * 6.0f, -3.5f, 3.5f);
            }
        }

        if (RandInt(0, 420) == 0) {
            int baseY = FloorYAt(s.baseX);
            SpawnBubble(static_cast<float>(s.baseX), static_cast<float>(baseY - s.height), RandInt(0, 1));
        }
    }
}

void Aquarium::UpdateFood(float dt) {
    for (auto it = foods_.begin(); it != foods_.end();) {
        it->life -= dt;
        it->y += it->vy * dt;
        it->x += std::sin(time_ * 2.5f + it->phase) * 0.9f * dt;
        it->x = Clamp(it->x, 1.0f, static_cast<float>(cols_ - 2));

        int fy = FloorYAt(static_cast<int>(it->x));
        if (it->y >= fy - 0.5f) {
            it->y = static_cast<float>(fy - 1);
            it->vy = 0.0f;
            it->life -= dt * 2.5f;
        }

        if (it->life <= 0.0f) {
            it = foods_.erase(it);
        } else {
            ++it;
        }
    }
}

void Aquarium::UpdateFish(float dt) {
    const float topBound = 5.0f;
    const int vb = VisibleBottom();

    for (size_t i = 0; i < fishes_.size(); ++i) {
        auto& f = fishes_[i];
        f.phase += dt * 4.2f;
        if (f.scaredTimer > 0.0f) f.scaredTimer -= dt;
        if (f.hungryCooldown > 0.0f) f.hungryCooldown -= dt;
        f.turnTimer -= dt;

        // 1. Seek nearest food pellet
        int targetFood = -1;
        float bestFoodDist = (f.species == FishSpecies::Minnow) ? 28.0f : 44.0f;
        if (f.scaredTimer <= 0.0f && f.hungryCooldown <= 0.0f && !foods_.empty()) {
            for (size_t k = 0; k < foods_.size(); ++k) {
                float dx = foods_[k].x - f.x;
                float dy = (foods_[k].y - f.y) * 1.9f;
                float d = std::sqrt(dx * dx + dy * dy);
                if (d < bestFoodDist) {
                    bestFoodDist = d;
                    targetFood = static_cast<int>(k);
                }
            }
        }

        if (targetFood >= 0) {
            const auto& food = foods_[targetFood];
            float dx = food.x - f.x;
            float dy = food.y - f.y;
            float dist = std::sqrt(dx * dx + dy * dy) + 0.01f;
            float chaseSpeed = f.baseSpeed * 1.65f;
            float desiredVx = (dx / dist) * chaseSpeed;
            float desiredVy = (dy / dist) * (chaseSpeed * 0.55f);
            f.vx += (desiredVx - f.vx) * std::min(1.0f, dt * 4.5f);
            f.vy += (desiredVy - f.vy) * std::min(1.0f, dt * 4.5f);

            if (dist < 2.6f) {
                Sparkle sp{ food.x, food.y, 0.45f, L'*' };
                sparkles_.push_back(sp);
                SpawnBubble(f.x, f.y - 0.5f, RandInt(0, 1));
                foods_.erase(foods_.begin() + targetFood);
                f.hungryCooldown = 0.6f;
            }
        } else {
            if (f.turnTimer <= 0.0f) {
                f.turnTimer = RandFloat(6.0f, 15.0f);
                if (RandInt(0, 2) == 0) f.facing = -f.facing;
                int maxY = std::max(9, vb - 10);
                f.swimTargetY = RandFloat(5.5f, static_cast<float>(maxY));
            }

            float targetVx = f.baseSpeed * f.facing;
            float targetVy = Clamp((f.swimTargetY - f.y) * 0.35f, -2.0f, 2.0f)
                           + std::sin(f.phase * 0.55f) * 0.55f;

            // Gentle schooling for minnows
            if (f.species == FishSpecies::Minnow && f.scaredTimer <= 0.0f) {
                float avgY = 0.0f, avgVx = 0.0f;
                int neighbors = 0;
                for (size_t j = 0; j < fishes_.size(); ++j) {
                    if (i == j || fishes_[j].species != FishSpecies::Minnow) continue;
                    float dx = fishes_[j].x - f.x;
                    float dy = fishes_[j].y - f.y;
                    float d2 = dx * dx + dy * dy * 3.0f;
                    if (d2 < 320.0f) {
                        avgY += fishes_[j].y;
                        avgVx += fishes_[j].vx;
                        neighbors++;
                        if (d2 < 24.0f && d2 > 0.01f) {
                            targetVx -= dx * 0.8f;
                            targetVy -= dy * 0.8f;
                        }
                    }
                }
                if (neighbors > 0) {
                    avgY /= neighbors;
                    avgVx /= neighbors;
                    targetVy += (avgY - f.y) * 0.25f;
                    targetVx = targetVx * 0.75f + avgVx * 0.25f;
                }
            }

            float relax = (f.scaredTimer > 0.0f) ? 1.6f : 2.8f;
            f.vx += (targetVx - f.vx) * std::min(1.0f, dt * relax);
            f.vy += (targetVy - f.vy) * std::min(1.0f, dt * relax);
        }

        // Mouse cursor reaction
        if (mouseActive_) {
            float dx = f.x - mouseX_;
            float dy = (f.y - mouseY_) * 1.9f;
            float dist = std::sqrt(dx * dx + dy * dy);
            float reactRadius = (mouseSpeed_ > 2.2f) ? 20.0f : 13.0f;

            if (dist < reactRadius && dist > 0.05f) {
                float nx = dx / dist;
                float ny = (f.y - mouseY_) / (dist + 0.01f);
                float strength = (1.0f - dist / reactRadius);

                if (mouseSpeed_ > 1.8f || dist < 7.0f) {
                    float burst = f.baseSpeed * (2.0f + strength * 1.4f);
                    f.vx += nx * burst * dt * 7.5f;
                    f.vy += ny * 10.0f * dt * 4.5f;
                    if (f.scaredTimer <= 0.1f && RandInt(0, 2) == 0) {
                        SpawnBubble(f.x, f.y, RandInt(0, 2));
                    }
                    f.scaredTimer = std::max(f.scaredTimer,
                        (f.species == FishSpecies::Puffer) ? 2.6f : 0.85f);
                } else {
                    f.vx += nx * f.baseSpeed * strength * dt * 3.0f;
                    f.vy += ny * 4.0f * strength * dt * 2.5f;
                }
            }
        }

        float maxSpd = f.baseSpeed * 2.8f;
        f.vx = Clamp(f.vx, -maxSpd, maxSpd);
        f.vy = Clamp(f.vy, -8.5f, 8.5f);

        f.x += f.vx * dt;
        f.y += f.vy * dt;

        if (f.vx > 0.8f) f.facing = 1;
        else if (f.vx < -0.8f) f.facing = -1;

        if (f.x < -14.0f) {
            f.x = static_cast<float>(cols_ + 10);
            f.swimTargetY = RandFloat(5.5f, static_cast<float>(std::max(9, vb - 10)));
        } else if (f.x > cols_ + 14.0f) {
            f.x = -10.0f;
            f.swimTargetY = RandFloat(5.5f, static_cast<float>(std::max(9, vb - 10)));
        }

        int fx = static_cast<int>(Clamp(f.x, 0.0f, static_cast<float>(cols_ - 1)));
        float bottomBound = static_cast<float>(FloorYAt(fx) - 4);
        if (f.species == FishSpecies::Angel || f.species == FishSpecies::ReefBig) {
            bottomBound -= 2.0f;
        }
        if (f.y < topBound) {
            f.y = topBound;
            if (f.vy < 0.0f) f.vy = -f.vy * 0.4f;
        } else if (f.y > bottomBound) {
            f.y = bottomBound;
            if (f.vy > 0.0f) f.vy = -f.vy * 0.4f;
        }

        if (RandInt(0, 720) == 0) {
            SpawnBubble(f.x + f.facing * 3.0f, f.y - 0.5f, 0);
        }
    }
}

void Aquarium::UpdateTurtle(float dt) {
    const int vb = VisibleBottom();
    turtle_.phase += dt * 2.2f;
    float targetVx = 3.0f * turtle_.facing;
    float targetVy = std::sin(turtle_.phase * 0.6f) * 0.7f;

    if (mouseActive_) {
        float dx = turtle_.x - mouseX_;
        float dy = (turtle_.y - mouseY_) * 1.8f;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 14.0f && dist > 0.1f) {
            targetVx += (dx / dist) * 4.5f;
            targetVy += ((turtle_.y - mouseY_) / dist) * 2.5f;
        }
    }

    turtle_.vx += (targetVx - turtle_.vx) * std::min(1.0f, dt * 1.8f);
    turtle_.vy += (targetVy - turtle_.vy) * std::min(1.0f, dt * 2.0f);
    turtle_.x += turtle_.vx * dt;
    turtle_.y += turtle_.vy * dt;

    if (turtle_.x > cols_ + 20.0f) {
        turtle_.facing = -1;
        turtle_.vx = -3.0f;
        turtle_.y = RandFloat(6.5f, static_cast<float>(std::max(10, vb / 2)));
    } else if (turtle_.x < -24.0f) {
        turtle_.facing = 1;
        turtle_.vx = 3.0f;
        turtle_.y = RandFloat(6.5f, static_cast<float>(std::max(10, vb / 2)));
    }
    turtle_.y = Clamp(turtle_.y, 6.0f, static_cast<float>(std::max(10, vb - 12)));

    if (RandInt(0, 350) == 0) {
        SpawnBubble(turtle_.x + turtle_.facing * 9.0f, turtle_.y, RandInt(1, 2));
    }
}

void Aquarium::UpdateSeahorses(float dt) {
    for (auto& sh : seahorses_) {
        sh.phase += dt * 1.8f;
        float targetX = sh.baseX + std::sin(sh.phase * 0.5f) * 3.0f;
        float targetY = sh.baseY + std::sin(sh.phase) * 1.6f;

        if (mouseActive_) {
            float dx = sh.x - mouseX_;
            float dy = (sh.y - mouseY_) * 1.8f;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < 11.0f && dist > 0.1f) {
                targetX += (dx / dist) * 4.0f;
                targetY += ((sh.y - mouseY_) / dist) * 2.0f;
            }
        }
        sh.x += (targetX - sh.x) * std::min(1.0f, dt * 2.5f);
        sh.y += (targetY - sh.y) * std::min(1.0f, dt * 2.5f);
    }
}

void Aquarium::UpdateJellyfish(float dt) {
    const int vb = VisibleBottom();
    for (auto& j : jellies_) {
        j.phase += dt * j.pulseSpeed;
        float pulse = std::sin(j.phase);
        float targetVy = (pulse > 0.25f) ? -2.3f * pulse : 1.05f;
        j.vy += (targetVy - j.vy) * std::min(1.0f, dt * 3.0f);
        j.vx += std::sin(time_ * 0.35f + j.phase) * 0.35f * dt;

        if (mouseActive_) {
            float dx = j.x - mouseX_;
            float dy = (j.y - mouseY_) * 1.8f;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < 13.0f && dist > 0.1f) {
                j.vx += (dx / dist) * 3.5f * dt;
                j.vy += ((j.y - mouseY_) / dist) * 2.5f * dt;
            }
        }

        j.vx = Clamp(j.vx, -2.2f, 2.2f);
        j.x += j.vx * dt;
        j.y += j.vy * dt;

        if (j.x < 5.0f) { j.x = 5.0f; j.vx = std::fabs(j.vx); }
        if (j.x > cols_ - 15.0f) { j.x = static_cast<float>(cols_ - 15); j.vx = -std::fabs(j.vx); }

        float maxY = static_cast<float>(std::max(12, vb - 12));
        if (j.y < 5.0f) { j.y = 5.0f; j.vy = 0.8f; }
        if (j.y > maxY) { j.y = maxY; j.vy = -1.8f; }
    }
}

void Aquarium::UpdateCrab(float dt) {
    crab_.animPhase += dt * 5.2f;
    if (crab_.startledTimer > 0.0f) crab_.startledTimer -= dt;

    float crabY = static_cast<float>(FloorYAt(static_cast<int>(crab_.x + 7)) - 2);
    if (mouseActive_) {
        float dx = (crab_.x + 7.0f) - mouseX_;
        float dy = (crabY - mouseY_) * 1.8f;
        if (std::sqrt(dx * dx + dy * dy) < 12.0f) {
            crab_.startledTimer = 1.5f;
            crab_.pauseTimer = 0.0f;
            crab_.vx = (dx >= 0.0f) ? 8.5f : -8.5f;
        }
    }

    if (crab_.pauseTimer > 0.0f) {
        crab_.pauseTimer -= dt;
        return;
    }

    crab_.x += crab_.vx * dt;
    if (crab_.x < 4.0f) {
        crab_.x = 4.0f;
        crab_.vx = RandFloat(2.5f, 4.2f);
        crab_.pauseTimer = RandFloat(1.2f, 3.2f);
    } else if (crab_.x > cols_ - 18.0f) {
        crab_.x = static_cast<float>(cols_ - 18);
        crab_.vx = -RandFloat(2.5f, 4.2f);
        crab_.pauseTimer = RandFloat(1.2f, 3.2f);
    } else if (crab_.startledTimer <= 0.0f && RandInt(0, 260) == 0) {
        crab_.pauseTimer = RandFloat(1.4f, 4.0f);
        if (RandInt(0, 1) == 0) crab_.vx = -crab_.vx;
        crab_.vx = Clamp(crab_.vx, -4.2f, 4.2f);
    } else if (crab_.startledTimer <= 0.0f) {
        crab_.vx = Clamp(crab_.vx, -4.2f, 4.2f);
    }
}

void Aquarium::UpdateChest(float dt) {
    int fy = FloorYAt(chest_.x + 6);
    if (mouseActive_) {
        float dx = (chest_.x + 6.0f) - mouseX_;
        float dy = (fy - 2.0f - mouseY_) * 1.8f;
        if (std::sqrt(dx * dx + dy * dy) < 9.0f && chest_.openTimer <= 0.0f) {
            chest_.openTimer = 3.0f;
            chest_.nextOpenIn = RandFloat(10.0f, 18.0f);
        }
    }

    if (chest_.openTimer > 0.0f) {
        chest_.openTimer -= dt;
        if (RandInt(0, 6) == 0) {
            SpawnBubble(chest_.x + RandFloat(3.0f, 9.0f), static_cast<float>(fy - 3), RandInt(1, 3), true);
        }
    } else {
        chest_.nextOpenIn -= dt;
        if (chest_.nextOpenIn <= 0.0f) {
            chest_.openTimer = 3.2f;
            chest_.nextOpenIn = RandFloat(11.0f, 20.0f);
        }
    }
}

void Aquarium::UpdateBubbles(float dt) {
    if (bubbles_.size() < 24 && RandInt(0, 24) == 0) {
        int x = RandInt(3, std::max(4, cols_ - 4));
        SpawnBubble(static_cast<float>(x), static_cast<float>(FloorYAt(x) - 1));
    }

    for (auto it = bubbles_.begin(); it != bubbles_.end();) {
        if (it->popping) {
            it->popTimer -= dt;
            if (it->popTimer <= 0.0f) {
                it = bubbles_.erase(it);
                continue;
            }
            ++it;
            continue;
        }

        it->y += it->vy * dt;
        it->x += std::sin(time_ * 3.1f + it->phase) * 1.35f * dt;

        if (it->y <= 3.0f) {
            it->y = 3.0f;
            it->popping = true;
            it->popTimer = 0.16f;
        }
        ++it;
    }
}

void Aquarium::UpdateRipples(float dt) {
    for (auto it = ripples_.begin(); it != ripples_.end();) {
        it->radius += dt * 11.0f;
        it->life -= dt * 1.4f;
        if (it->life <= 0.0f || it->radius >= it->maxRadius) {
            it = ripples_.erase(it);
        } else {
            ++it;
        }
    }
}

void Aquarium::UpdateSparkles(float dt) {
    for (auto it = sparkles_.begin(); it != sparkles_.end();) {
        it->life -= dt;
        it->y -= dt * 1.8f;
        if (it->life <= 0.0f) {
            it = sparkles_.erase(it);
        } else {
            ++it;
        }
    }
}

void Aquarium::ClearGrid() {
    for (auto& c : grid_) c.ch = L' ';
}

void Aquarium::PutChar(int x, int y, wchar_t ch, COLORREF color) {
    if (x < 0 || x >= cols_ || y < 0 || y >= rows_) return;
    if (ch == L' ') return;
    Cell& cell = grid_[static_cast<size_t>(y) * cols_ + x];
    cell.ch = ch;
    cell.color = color;
}

void Aquarium::PutString(int x, int y, const wchar_t* str, COLORREF color) {
    if (y < 0 || y >= rows_) return;
    for (int i = 0; str[i] != L'\0'; ++i) {
        if (str[i] != L' ') PutChar(x + i, y, str[i], color);
    }
}

// Multi-color fish line: eyes ('o'/'°'/'º') get eyeCol, stripes ('|') get accentCol, body gets bodyCol
void Aquarium::PutFishLine(int x, int y, const wchar_t* str, COLORREF bodyCol, COLORREF accentCol, COLORREF eyeCol) {
    if (y < 0 || y >= rows_) return;
    for (int i = 0; str[i] != L'\0'; ++i) {
        wchar_t c = str[i];
        if (c == L' ') continue;
        COLORREF col = bodyCol;
        if (c == L'o' || c == L'\x00B0' || c == L'\x00BA' || c == L'@') col = eyeCol;
        else if (c == L'|' || c == L')' || c == L'(' || c == L'=') col = accentCol;
        PutChar(x + i, y, c, col);
    }
}

void Aquarium::DrawLightRays() {
    const Theme& t = CurrentTheme();
    const int vb = VisibleBottom();
    for (int r = 0; r < 5; ++r) {
        float base = cols_ * (0.10f + 0.20f * r) + std::sin(time_ * 0.22f + r * 1.6f) * 7.0f;
        int depth = std::min(vb - 8, 16 + (r % 2) * 6);
        for (int y = 4; y < depth; ++y) {
            float offset = (y - 4) * 0.42f;
            int x = static_cast<int>(base + offset);
            if ((y + r) % 2 == 0) {
                wchar_t ch = (y < depth - 5) ? L':' : L'.';
                PutChar(x, y, ch, t.lightRay);
                PutChar(x + 2, y, L'\'', t.lightRay);
                if (y < depth - 8) PutChar(x + 4, y, L'.', t.lightRay);
            }
        }
    }
}

void Aquarium::DrawSurface() {
    const Theme& t = CurrentTheme();
    for (int x = 0; x < cols_; ++x) {
        float w1 = std::sin(x * 0.14f + time_ * 2.0f);
        float w2 = std::cos(x * 0.08f - time_ * 1.3f);
        float sum = w1 + w2;

        wchar_t topCh = L'~';
        COLORREF topCol = t.waterSurface;
        if (sum > 1.25f) {
            topCh = L'^';
            topCol = t.waterFoam;
        } else if (sum > 0.65f) {
            topCh = L'\x2248'; // ≈
        } else if (sum < -1.1f) {
            topCh = L'_';
        }
        PutChar(x, 2, topCh, topCol);

        if (((x + static_cast<int>(time_ * 3.5f)) % 5) == 0 && w1 > 0.05f) {
            PutChar(x, 3, (sum > 0.9f) ? L'~' : L'-', t.waterSurface);
        }
    }
}

void Aquarium::DrawSeabed() {
    const Theme& t = CurrentTheme();

    // Sand terrain (raised cleanly above taskbar, with deep sand continuing below)
    for (int x = 0; x < cols_; ++x) {
        int fy = FloorYAt(x);
        PutChar(x, fy, ((x % 4) == 0) ? L'~' : L'_', t.sand);
        for (int y = fy + 1; y < rows_; ++y) {
            int dy = y - fy;
            wchar_t ch = sandNoise_[static_cast<size_t>(x) * 8 + (dy % 8)];
            PutChar(x, y, ch, (dy <= 1) ? t.sand : t.sandDark);
        }
    }

    // Shells & starfish on sand
    for (const auto& sd : shells_) {
        int fy = FloorYAt(sd.x);
        COLORREF col = sd.star ? t.coral1 : t.sand;
        PutChar(sd.x, fy + sd.dy, sd.ch, col);
    }

    // Detailed shaded ASCII rocks
    for (const auto& r : rocks_) {
        int fy = FloorYAt(r.x + r.width / 2);
        if (r.height >= 3 && r.width >= 9) {
            for (int dx = 3; dx < r.width - 3; ++dx)
                PutChar(r.x + dx, fy - 3, L'_', t.rockLight);
            PutChar(r.x + 2, fy - 2, L'/', t.rockLight);
            PutChar(r.x + r.width - 3, fy - 2, L'\\', t.rock);
            for (int dx = 3; dx < r.width - 3; ++dx)
                PutChar(r.x + dx, fy - 2, (dx == 3) ? L'.' : ((dx % 3 == 0) ? L'`' : L' '), t.rockLight);
        }
        PutChar(r.x + 1, fy - 1, L'/', t.rockLight);
        PutChar(r.x + r.width - 2, fy - 1, L'\\', t.rock);
        for (int dx = 2; dx < r.width - 2; ++dx)
            PutChar(r.x + dx, fy - 1, (dx % 2 == 0) ? L'.' : L':', t.rock);
        PutChar(r.x, fy, L'/', t.rock);
        PutChar(r.x + r.width - 1, fy, L'\\', t.rock);
        for (int dx = 1; dx < r.width - 1; ++dx)
            PutChar(r.x + dx, fy, L'_', t.rock);
    }

    // Detailed branching staghorn & fan corals
    for (const auto& c : corals_) {
        int fy = FloorYAt(c.x);
        COLORREF col = (c.colorSlot == 0) ? t.coral1 : ((c.colorSlot == 1) ? t.coral2 : t.coral3);
        if (c.style == 0) {
            // Branching staghorn coral
            PutString(c.x - 2, fy - 3, L"*   *", col);
            PutString(c.x - 2, fy - 2, L"\\ * /", col);
            PutString(c.x - 1, fy - 1, L"\\|/", col);
            PutString(c.x,     fy,     L"Y", col);
        } else if (c.style == 1) {
            // Sea fan coral
            PutString(c.x - 2, fy - 3, L".o*o.", col);
            PutString(c.x - 3, fy - 2, L"o\\ | /o", col);
            PutString(c.x - 2, fy - 1, L"\\ \\|/ /", col);
            PutString(c.x - 1, fy,     L"\\|/", col);
        } else {
            // Tube anemone / brain coral cluster
            PutString(c.x - 2, fy - 2, L"\\|/|/", col);
            PutString(c.x - 2, fy - 1, L"(~~~)", col);
            PutString(c.x - 1, fy,     L"|||", col);
        }
    }
}

void Aquarium::DrawTreasureChest() {
    const Theme& t = CurrentTheme();
    int cx = chest_.x;
    int fy = FloorYAt(cx + 6);
    bool open = (chest_.openTimer > 0.0f);

    if (open) {
        // Lid popped open with shimmering gold inside!
        PutString(cx + 1, fy - 4, L".---------.", t.chestWood);
        PutString(cx,     fy - 3, L"/  * o * o \\", t.chestGold);
        PutString(cx,     fy - 2, L"|===[===]===|", t.chestGold);
        PutString(cx,     fy - 1, L"|  |  o  |  |", t.chestWood);
        PutString(cx,     fy,     L"|__|_____|__|", t.chestWood);
    } else {
        PutString(cx + 1, fy - 3, L".---------.", t.chestWood);
        PutString(cx,     fy - 2, L"/===========\\", t.chestWood);
        PutString(cx,     fy - 1, L"|===[ o ]===|", t.chestGold);
        PutString(cx,     fy,     L"|___________|", t.chestWood);
    }
}

void Aquarium::DrawSeaweed(bool brightLayer) {
    const Theme& t = CurrentTheme();
    for (const auto& s : seaweeds_) {
        if (s.bright != brightLayer) continue;
        int baseY = FloorYAt(s.baseX);
        COLORREF mainCol = s.bright ? t.seaweedLight : t.seaweedDark;
        COLORREF leafCol = s.bright ? t.seaweedDark : t.seaweedLight;

        for (int seg = 0; seg < s.height; ++seg) {
            float ratio = static_cast<float>(seg) / static_cast<float>(std::max(1, s.height));
            float sway = std::sin(time_ * s.speed + s.phase + seg * 0.42f) * (0.4f + ratio * 1.6f);
            float bendOffset = s.bend * ratio * ratio;
            int x = s.baseX + static_cast<int>(std::round(sway + bendOffset));
            int y = baseY - seg;

            if (seg == s.height - 1) {
                PutChar(x, y, L'~', mainCol);
                if (s.leafy) PutChar(x + 1, y, L'`', leafCol);
                continue;
            }

            float slope = std::cos(time_ * s.speed + s.phase + seg * 0.42f) + s.bend * 0.25f;
            wchar_t stem = (slope > 0.35f) ? L')' : ((slope < -0.35f) ? L'(' : ((seg % 2 == 0) ? L'S' : L'|'));
            PutChar(x, y, stem, mainCol);

            // Alternating side leaves for lush kelp look
            if (s.leafy && seg > 0 && (seg % 2 == 0)) {
                if ((seg / 2) % 2 == 0) PutChar(x - 1, y, L'(', leafCol);
                else PutChar(x + 1, y, L')', leafCol);
            }
        }
    }
}

void Aquarium::DrawSeahorses() {
    const Theme& t = CurrentTheme();
    for (const auto& sh : seahorses_) {
        int ix = static_cast<int>(std::round(sh.x));
        int iy = static_cast<int>(std::round(sh.y));
        COLORREF col = t.fishColors[sh.colorIndex % 6];
        if (sh.facing >= 0) {
            PutFishLine(ix,     iy,     L",o>=", col, col, t.fishEye);
            PutString  (ix,     iy + 1, L"(( )", col);
            PutString  (ix + 1, iy + 2, L"\\|",  col);
            PutString  (ix + 1, iy + 3, L"(@)",  col);
        } else {
            PutFishLine(ix,     iy,     L"=<o,", col, col, t.fishEye);
            PutString  (ix,     iy + 1, L"( ))", col);
            PutString  (ix + 1, iy + 2, L"|/",   col);
            PutString  (ix,     iy + 3, L"(@)",  col);
        }
    }
}

void Aquarium::DrawTurtle() {
    const Theme& t = CurrentTheme();
    int ix = static_cast<int>(std::round(turtle_.x));
    int iy = static_cast<int>(std::round(turtle_.y));
    bool strokeDown = std::sin(turtle_.phase) > 0.0f;

    if (turtle_.facing >= 0) {
        PutString  (ix + 3,  iy - 1, L".-------.",        t.turtleShell);
        PutString  (ix + 2,  iy,     L"/  /###\\  \\",    t.turtleShell);
        PutString  (ix + 14, iy,     L".-.",              t.turtleBody);
        PutString  (ix,      iy + 1, L"=|  |#####|  |",   t.turtleShell);
        PutFishLine(ix + 13, iy + 1, L"-(o>",             t.turtleBody, t.turtleBody, t.fishEye);
        if (strokeDown) {
            PutString(ix + 2, iy + 2, L"\\__/   \\__/",   t.turtleBody);
        } else {
            PutString(ix + 2, iy + 2, L" /_\\   /_\\",   t.turtleBody);
        }
    } else {
        PutString  (ix + 5,  iy - 1, L".-------.",        t.turtleShell);
        PutString  (ix,      iy,     L".-.",              t.turtleBody);
        PutString  (ix + 4,  iy,     L"/  /###\\  \\",    t.turtleShell);
        PutFishLine(ix,      iy + 1, L"<o)-",             t.turtleBody, t.turtleBody, t.fishEye);
        PutString  (ix + 4,  iy + 1, L"|  |#####|  |=",   t.turtleShell);
        if (strokeDown) {
            PutString(ix + 5, iy + 2, L"\\__/   \\__/",   t.turtleBody);
        } else {
            PutString(ix + 5, iy + 2, L" /_\\   /_\\",   t.turtleBody);
        }
    }
}

void Aquarium::DrawJellyfish() {
    const Theme& t = CurrentTheme();
    for (const auto& j : jellies_) {
        int ix = static_cast<int>(std::round(j.x));
        int iy = static_cast<int>(std::round(j.y));
        bool contracted = std::sin(j.phase) > 0.1f;

        if (contracted) {
            PutString(ix + 2, iy,     L".-----.",     t.jellyfishGlow);
            PutString(ix + 1, iy + 1, L"/  o o  \\",  t.jellyfish);
            PutString(ix,     iy + 2, L"(_________)", t.jellyfishGlow);
            PutString(ix + 2, iy + 3, L"| : | : |",   t.jellyfish);
            PutString(ix + 2, iy + 4, L"~   ~   ~",   t.jellyfish);
        } else {
            PutString(ix + 1, iy,     L"_.-----._",   t.jellyfishGlow);
            PutString(ix,     iy + 1, L"/   o o   \\",t.jellyfish);
            PutString(ix,     iy + 2, L"(_________)", t.jellyfishGlow);
            PutString(ix + 1, iy + 3, L"| ~ | ~ |",   t.jellyfish);
            PutString(ix + 1, iy + 4, L"~ : ~ : ~",   t.jellyfish);
        }
    }
}

void Aquarium::DrawFish() {
    const Theme& t = CurrentTheme();

    for (const auto& f : fishes_) {
        int ix = static_cast<int>(std::round(f.x));
        int iy = static_cast<int>(std::round(f.y));
        COLORREF bodyCol = t.fishColors[f.colorIndex % 6];
        COLORREF accentCol = t.fishColors[(f.colorIndex + 1) % 6];
        COLORREF eyeCol = t.fishEye;
        bool right = (f.facing >= 0);
        bool wag = (static_cast<int>(f.phase) % 2 == 0);

        switch (f.species) {
            case FishSpecies::Minnow: {
                // Sleek classic small schooling fish
                if (right) {
                    const wchar_t* s = (f.variant == 0) ? (wag ? L"><>" : L">=>")
                                     : (f.variant == 1) ? (wag ? L"><\x00B0>" : L">=\x00B0>")
                                                        : (wag ? L"><(\x00B0>" : L">=(\x00B0>");
                    PutFishLine(ix - 2, iy, s, bodyCol, bodyCol, eyeCol);
                } else {
                    const wchar_t* s = (f.variant == 0) ? (wag ? L"<><" : L"<=<")
                                     : (f.variant == 1) ? (wag ? L"<\x00B0><" : L"<\x00B0=<")
                                                        : (wag ? L"<\x00B0)><" : L"<\x00B0)=<");
                    PutFishLine(ix - 2, iy, s, bodyCol, bodyCol, eyeCol);
                }
                break;
            }
            case FishSpecies::Tropical: {
                // Classic iconic sleek ASCII tropical fish (1-line, vibrant & crisp)
                if (right) {
                    const wchar_t* s = (f.variant == 0) ? (wag ? L"><((\x00BA>" : L">=((\x00BA>")
                                     : (f.variant == 1) ? (wag ? L"><(((\x00B0>" : L">=(((\x00B0>")
                                                        : (wag ? L"><(((*>" : L">=(((*>");
                    PutFishLine(ix - 3, iy, s, bodyCol, accentCol, eyeCol);
                } else {
                    const wchar_t* s = (f.variant == 0) ? (wag ? L"<\x00BA))><" : L"<\x00BA))=<")
                                     : (f.variant == 1) ? (wag ? L"<\x00B0)))><" : L"<\x00B0)))=<")
                                                        : (wag ? L"<*)))><" : L"<*)))=<");
                    PutFishLine(ix - 3, iy, s, bodyCol, accentCol, eyeCol);
                }
                break;
            }
            case FishSpecies::Clownfish: {
                // Sleek striped clownfish (classic 1-line with bright white stripes!)
                COLORREF stripeCol = eyeCol;
                if (right) {
                    PutFishLine(ix - 4, iy, wag ? L"><(|(|o>" : L">=(|(|o>", bodyCol, stripeCol, eyeCol);
                } else {
                    PutFishLine(ix - 4, iy, wag ? L"<o|)|)><" : L"<o|)|)=<", bodyCol, stripeCol, eyeCol);
                }
                break;
            }
            case FishSpecies::Puffer: {
                if (f.scaredTimer > 0.0f) {
                    // Compact cute spiny pufferball when startled!
                    PutString  (ix - 3, iy - 1, L" \\.-./ ",  accentCol);
                    PutFishLine(ix - 3, iy,     L"-(o_o)-",   bodyCol, accentCol, eyeCol);
                    PutString  (ix - 3, iy + 1, L" /'-'\\ ",  accentCol);
                } else if (right) {
                    PutFishLine(ix - 3, iy, wag ? L"><(\x00BA)\x00B0>" : L">=(\x00BA)\x00B0>", bodyCol, accentCol, eyeCol);
                } else {
                    PutFishLine(ix - 3, iy, wag ? L"<\x00B0(\x00BA)><" : L"<\x00B0(\x00BA)=<", bodyCol, accentCol, eyeCol);
                }
                break;
            }
            case FishSpecies::Angel: {
                // Clean, proportional 3-row diamond angelfish
                if (right) {
                    PutString  (ix - 1, iy - 1, L"  _/\\",          accentCol);
                    PutFishLine(ix - 3, iy,     wag ? L"\\ / | o>" : L"/ / | o>", bodyCol, accentCol, eyeCol);
                    PutString  (ix - 3, iy + 1, wag ? L"/ \\_|_/"  : L"\\ \\_|_/", bodyCol);
                } else {
                    PutString  (ix - 2, iy - 1, L" /\\_",           accentCol);
                    PutFishLine(ix - 4, iy,     wag ? L"<o | \\ /" : L"<o | \\ \\", bodyCol, accentCol, eyeCol);
                    PutString  (ix - 3, iy + 1, wag ? L"\\_|_/ \\"  : L"\\_|_/ /",  bodyCol);
                }
                break;
            }
            case FishSpecies::ReefBig: {
                // Sleek curved 3-row classic ASCII reef fish
                if (right) {
                    PutString  (ix - 2, iy - 1, L"  _.---.",             accentCol);
                    PutFishLine(ix - 4, iy,     wag ? L"\\ / ))) o\\" : L"/ / ))) o\\", bodyCol, accentCol, eyeCol);
                    PutString  (ix - 4, iy + 1, wag ? L"/ \\_____./"  : L"\\ \\_____./", bodyCol);
                } else {
                    PutString  (ix - 3, iy - 1, L" .---._",              accentCol);
                    PutFishLine(ix - 4, iy,     wag ? L"/o ((( \\ /"  : L"/o ((( \\ \\", bodyCol, accentCol, eyeCol);
                    PutString  (ix - 4, iy + 1, wag ? L"\\._____/ \\" : L"\\._____/ /",  bodyCol);
                }
                break;
            }
        }
    }
}

void Aquarium::DrawCrab() {
    const Theme& t = CurrentTheme();
    int ix = static_cast<int>(std::round(crab_.x));
    int iy = FloorYAt(ix + 7) - 1;
    bool step = (static_cast<int>(crab_.animPhase) % 2 == 0);

    if (crab_.startledTimer > 0.0f) {
        PutString  (ix + 1, iy - 2, L"\\\\  _  //",     t.crab);
        PutFishLine(ix + 1, iy - 1, L" \\(o_o)/ ",     t.crab, t.crab, t.fishEye);
        PutString  (ix,     iy,     L"/ /___\\ \\",     t.crab);
    } else if (step) {
        PutString  (ix + 1, iy - 2, L"(\\/) (\\/)",     t.crab);
        PutFishLine(ix,     iy - 1, L"/ (o,,o) \\",     t.crab, t.crab, t.fishEye);
        PutString  (ix,     iy,     L"//  \\/  \\\\",   t.crab);
    } else {
        PutString  (ix + 1, iy - 2, L" (v)   (v)",      t.crab);
        PutFishLine(ix,     iy - 1, L"/ (o,,o) \\",     t.crab, t.crab, t.fishEye);
        PutString  (ix,     iy,     L"\\\\  \\/  //",   t.crab);
    }
}

void Aquarium::DrawFood() {
    const Theme& t = CurrentTheme();
    for (const auto& f : foods_) {
        PutChar(static_cast<int>(std::round(f.x)),
                static_cast<int>(std::round(f.y)),
                f.ch, t.food);
    }
}

void Aquarium::DrawBubbles() {
    const Theme& t = CurrentTheme();
    const wchar_t chars[4] = { L'.', L'\x00B0', L'o', L'O' };
    for (const auto& b : bubbles_) {
        int ix = static_cast<int>(std::round(b.x));
        int iy = static_cast<int>(std::round(b.y));
        wchar_t ch = b.popping ? L'*' : chars[b.size % 4];
        PutChar(ix, iy, ch, b.golden ? t.chestGold : t.bubble);
    }
}

void Aquarium::DrawRipples() {
    const Theme& t = CurrentTheme();
    for (const auto& rp : ripples_) {
        int rx = static_cast<int>(std::round(rp.radius));
        int ry = std::max(1, rx / 2);
        int cx = static_cast<int>(std::round(rp.x));
        int cy = static_cast<int>(std::round(rp.y));

        PutChar(cx - rx, cy, L'(', t.ripple);
        PutChar(cx + rx, cy, L')', t.ripple);
        if (rx >= 3) {
            PutChar(cx, cy - ry, L'~', t.ripple);
            PutChar(cx, cy + ry, L'~', t.ripple);
        }
    }
}

void Aquarium::DrawSparkles() {
    const Theme& t = CurrentTheme();
    for (const auto& sp : sparkles_) {
        PutChar(static_cast<int>(std::round(sp.x)),
                static_cast<int>(std::round(sp.y)),
                sp.ch, t.food);
    }
}

void Aquarium::Render() {
    if (cols_ <= 0 || rows_ <= 0) return;
    ClearGrid();
    DrawLightRays();
    DrawSeaweed(false); // background seaweed
    DrawSeabed();
    DrawTreasureChest();
    DrawSeahorses();
    DrawTurtle();
    DrawJellyfish();
    DrawFish();
    DrawSeaweed(true);  // foreground seaweed
    DrawCrab();
    DrawFood();
    DrawBubbles();
    DrawRipples();
    DrawSparkles();
    DrawSurface();
}
