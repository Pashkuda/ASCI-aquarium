#include "aquarium.h"
#include <cmath>
#include <algorithm>
#include <chrono>

namespace {
constexpr float PI = 3.14159265358979323846f;

float Clamp(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi ? hi : v);
}

COLORREF LerpColor(COLORREF a, COLORREF b, float t) {
    t = Clamp(t, 0.0f, 1.0f);
    int r = static_cast<int>(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t);
    int g = static_cast<int>(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t);
    int bl = static_cast<int>(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t);
    return RGB(std::max(0, std::min(255, r)),
               std::max(0, std::min(255, g)),
               std::max(0, std::min(255, bl)));
}

COLORREF ScaleColor(COLORREF c, float s) {
    int r = static_cast<int>(GetRValue(c) * s);
    int g = static_cast<int>(GetGValue(c) * s);
    int b = static_cast<int>(GetBValue(c) * s);
    return RGB(std::max(0, std::min(255, r)),
               std::max(0, std::min(255, g)),
               std::max(0, std::min(255, b)));
}
} // namespace

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

COLORREF Aquarium::BgColorAtRow(int y) const {
    const Theme& t = CurrentTheme();
    float k = Clamp(static_cast<float>(y) / static_cast<float>(std::max(1, rows_ - 1)), 0.0f, 1.0f);
    return LerpColor(t.bgTop, t.bgBottom, std::pow(k, 0.85f));
}

int Aquarium::FloorYAt(int x) const {
    int vb = VisibleBottom();
    if (floorHeights_.empty()) return vb - 3;
    int ix = (x < 0) ? 0 : (x >= cols_ ? cols_ - 1 : x);
    return floorHeights_[ix];
}

float Aquarium::FloorHeightFAt(float x) const {
    int vb = VisibleBottom();
    if (floorHeightF_.empty()) return static_cast<float>(vb - 3);
    float fx = Clamp(x, 0.0f, static_cast<float>(cols_ - 1));
    int i0 = static_cast<int>(fx);
    int i1 = std::min(cols_ - 1, i0 + 1);
    float frac = fx - static_cast<float>(i0);
    return floorHeightF_[i0] * (1.0f - frac) + floorHeightF_[i1] * frac;
}

float Aquarium::SurfaceWaveAt(float x) const {
    // Smooth multi-harmonic ocean swell + gentle counter-ripples
    float w = std::sin(x * 0.075f + time_ * 1.45f) * 0.46f
            + std::sin(x * 0.165f - time_ * 2.05f + 0.8f) * 0.28f
            + std::sin(x * 0.038f + time_ * 0.72f + 2.1f) * 0.26f;

    // Interactive wave disturbance from mouse or food splash near surface
    if (surfaceDisturbAmp_ > 0.01f) {
        float dx = x - surfaceDisturbX_;
        float env = std::exp(-(dx * dx) / 220.0f);
        w += std::sin(std::fabs(dx) * 0.55f - time_ * 7.5f) * env * surfaceDisturbAmp_;
    }
    return w;
}

float Aquarium::CurrentXAt(float x, float y) const {
    // Gentle, slowly shifting underwater current field
    float depthFactor = 0.65f + 0.35f * std::cos(y * 0.09f);
    return (std::sin(time_ * 0.24f + y * 0.05f) * 0.75f
          + std::sin(time_ * 0.11f + x * 0.015f) * 0.40f) * depthFactor;
}

float Aquarium::LightIntensityAt(float x, float y) const {
    const int vb = VisibleBottom();
    if (y < 3.0f || y > static_cast<float>(vb + 2)) return 0.0f;

    float depthFrac = Clamp((y - 3.0f) / static_cast<float>(std::max(10, vb - 4)), 0.0f, 1.0f);
    float attenuation = std::pow(1.0f - depthFrac * 0.82f, 1.25f);

    float total = 0.0f;
    // 5 gently drifting volumetric diagonal sunbeams
    constexpr int RAY_COUNT = 5;
    for (int r = 0; r < RAY_COUNT; ++r) {
        float anchor = cols_ * (0.08f + 0.21f * r)
                     + std::sin(time_ * (0.18f + 0.03f * r) + r * 1.9f) * 6.5f;
        float slant = (y - 3.0f) * 0.44f;
        float center = anchor + slant;
        float width = 5.5f + depthFrac * 5.2f + (r % 2) * 1.4f;
        float dx = (x - center) / width;
        float beam = std::exp(-dx * dx * 1.7f);
        float shimmer = 0.74f + 0.26f * std::sin(time_ * 1.6f + r * 2.3f + SurfaceWaveAt(anchor) * 1.6f);
        total += beam * shimmer;
    }
    return Clamp(total * attenuation, 0.0f, 1.0f);
}

void Aquarium::Populate() {
    const int vb = VisibleBottom();
    floorHeights_.resize(cols_);
    floorHeightF_.resize(cols_);
    bgFloorF_.resize(cols_);
    sandNoise_.resize(static_cast<size_t>(cols_) * 10);

    // Smooth continuous rolling dunes placed cleanly ABOVE the Windows taskbar
    float p1 = RandFloat(0.0f, 2.0f * PI);
    float p2 = RandFloat(0.0f, 2.0f * PI);
    float p3 = RandFloat(0.0f, 2.0f * PI);
    float bp1 = RandFloat(0.0f, 2.0f * PI);
    float bp2 = RandFloat(0.0f, 2.0f * PI);

    for (int x = 0; x < cols_; ++x) {
        float h = 3.2f
                + std::sin(x * 0.028f + p1) * 1.35f
                + std::sin(x * 0.072f + p2) * 0.65f
                + std::sin(x * 0.145f + p3) * 0.22f;
        float fy = static_cast<float>(vb - 1) - h;
        fy = Clamp(fy, static_cast<float>(vb - 6), static_cast<float>(vb - 2));
        floorHeightF_[x] = fy;
        floorHeights_[x] = static_cast<int>(std::round(fy));

        // Distant softer background reef/dune ridge (1.5..3.5 rows higher)
        float bh = h + 2.2f
                 + std::sin(x * 0.021f + bp1) * 1.15f
                 + std::sin(x * 0.058f + bp2) * 0.55f;
        bgFloorF_[x] = Clamp(static_cast<float>(vb - 1) - bh,
                             static_cast<float>(vb - 10),
                             fy - 1.2f);
    }

    const wchar_t sandChars[] = { L'.', L',', L' ', L' ', L' ', L' ', L'~', L'\x00B7', L' ', L' ' };
    for (size_t i = 0; i < sandNoise_.size(); ++i) {
        sandNoise_[i] = sandChars[RandInt(0, 9)];
    }

    // Treasure chest resting on a gently flattened pocket on the right-center seabed
    chest_.x = std::max(18, static_cast<int>(cols_ * 0.72f));
    chest_.openTimer = 0.0f;
    chest_.nextOpenIn = RandFloat(5.5f, 12.0f);
    int chestBaseY = FloorYAt(chest_.x + 6);
    for (int x = std::max(0, chest_.x - 2); x < std::min(cols_, chest_.x + 15); ++x) {
        floorHeightF_[x] = floorHeightF_[x] * 0.35f + static_cast<float>(chestBaseY) * 0.65f;
        floorHeights_[x] = static_cast<int>(std::round(floorHeightF_[x]));
    }

    // Detailed layered shaded boulders
    rocks_.clear();
    int rockCount = std::max(4, cols_ / 36);
    for (int i = 0; i < rockCount; ++i) {
        Rock r{};
        r.width = RandInt(9, 15);
        r.height = RandInt(3, 4);
        r.mossy = (i % 2 == 0);
        int candidate = RandInt(5, std::max(10, cols_ - r.width - 5));
        if (std::abs(candidate - chest_.x) < 16) {
            candidate = std::max(5, (candidate + 30) % std::max(16, cols_ - r.width - 5));
        }
        r.x = candidate;
        rocks_.push_back(r);
    }

    // Rich corals + swaying sea anemones (clownfish homes!)
    corals_.clear();
    std::vector<int> anemoneIndices;
    int coralCount = std::max(7, cols_ / 22);
    for (int i = 0; i < coralCount; ++i) {
        Coral c{};
        float slot = (static_cast<float>(i) + RandFloat(0.18f, 0.82f)) / static_cast<float>(coralCount);
        c.x = static_cast<int>(slot * (cols_ - 12)) + 6;
        if (std::abs(c.x - (chest_.x + 6)) < 11) continue;
        c.height = RandInt(3, 5);
        c.style = i % 4; // 0: staghorn, 1: sea fan, 2: animated sea anemone, 3: tube sponge
        c.colorSlot = i % 3;
        c.phase = RandFloat(0.0f, 2.0f * PI);
        if (c.style == 2) anemoneIndices.push_back(static_cast<int>(corals_.size()));
        corals_.push_back(c);
    }

    // Shells, starfish & smooth seabed pebbles
    shells_.clear();
    int shellCount = std::max(14, cols_ / 13);
    for (int i = 0; i < shellCount; ++i) {
        ShellDecor sd{};
        sd.x = RandInt(3, std::max(6, cols_ - 4));
        sd.dy = RandInt(0, 2);
        sd.kind = i % 3;
        if (sd.kind == 1) sd.ch = L'*';
        else if (sd.kind == 0) sd.ch = (i % 2 == 0) ? L'@' : L'c';
        else sd.ch = (i % 2 == 0) ? L'o' : L'\x00B0';
        shells_.push_back(sd);
    }

    // Short swaying seagrass clusters along dunes
    seagrasses_.clear();
    int grassCount = std::max(18, cols_ / 8);
    for (int i = 0; i < grassCount; ++i) {
        Seagrass g{};
        g.x = RandInt(2, std::max(4, cols_ - 3));
        if (std::abs(g.x - (chest_.x + 6)) < 8) continue;
        g.height = RandInt(2, 3);
        g.phase = RandFloat(0.0f, 2.0f * PI);
        g.bright = (i % 3 == 0);
        seagrasses_.push_back(g);
    }

    // Lush swaying kelp fronds (background & foreground)
    seaweeds_.clear();
    int weedCount = std::max(14, cols_ / 10);
    for (int i = 0; i < weedCount; ++i) {
        Seaweed s{};
        float slot = (static_cast<float>(i) + RandFloat(0.12f, 0.88f)) / static_cast<float>(weedCount);
        s.baseX = static_cast<int>(slot * (cols_ - 8)) + 4;
        if (std::abs(s.baseX - (chest_.x + 6)) < 9) continue;
        int maxH = std::max(7, (vb - 10) / 3);
        s.height = RandInt(std::max(5, maxH / 2), maxH);
        s.phase = RandFloat(0.0f, 2.0f * PI);
        s.speed = RandFloat(1.1f, 1.85f);
        s.bend = 0.0f;
        s.bright = (i % 2 == 0);
        s.leafy = (i % 3 != 0);
        seaweeds_.push_back(s);
    }

    // Ambient marine snow specks
    specks_.clear();
    int speckCount = std::max(18, cols_ / 9);
    for (int i = 0; i < speckCount; ++i) {
        MarineSpeck ms{};
        ms.x = RandFloat(1.0f, static_cast<float>(cols_ - 2));
        ms.y = RandFloat(5.0f, static_cast<float>(std::max(8, vb - 5)));
        ms.vx = RandFloat(-0.35f, 0.35f);
        ms.vy = RandFloat(0.08f, 0.30f);
        ms.phase = RandFloat(0.0f, 2.0f * PI);
        ms.ch = (i % 3 == 0) ? L'\x00B7' : L'.';
        specks_.push_back(ms);
    }

    // Fish ecosystem with depth layers & species-distinct personalities
    fishes_.clear();
    int minnowCount   = std::max(14, cols_ / 13);
    int tropicalCount = std::max(8, cols_ / 22);
    int clownCount    = std::max(3, cols_ / 52);
    int pufferCount   = 2;
    int angelCount    = 3;
    int reefBigCount  = 3;

    int depthBand = 0;
    int clownIdx = 0;
    auto addFish = [&](FishSpecies sp) {
        Fish f{};
        f.species = sp;
        f.x = RandFloat(8.0f, static_cast<float>(std::max(14, cols_ - 12)));
        int minY = 6;
        int maxY = std::max(minY + 5, vb - 11);
        float bandFrac = (static_cast<float>(depthBand % 3) + RandFloat(0.12f, 0.88f)) / 3.0f;
        depthBand++;
        f.y = minY + bandFrac * static_cast<float>(maxY - minY);
        f.facing = (RandInt(0, 1) == 0) ? 1 : -1;
        f.desiredFacing = f.facing;
        f.depthLayer = RandInt(0, 2);
        f.homeX = f.x;
        f.homeY = f.y;

        switch (sp) {
            case FishSpecies::Minnow:
                f.baseSpeed = RandFloat(8.5f, 12.5f);
                f.colorIndex = (RandInt(0, 2) == 0) ? 5 : RandInt(1, 5);
                f.variant = RandInt(0, 2);
                break;
            case FishSpecies::Clownfish:
                f.baseSpeed = RandFloat(5.2f, 7.6f);
                f.colorIndex = 0; // iconic clownfish orange
                f.variant = 0;
                f.depthLayer = (clownIdx % 2 == 0) ? 2 : 1;
                if (!anemoneIndices.empty()) {
                    const auto& anem = corals_[anemoneIndices[clownIdx % anemoneIndices.size()]];
                    f.homeX = static_cast<float>(anem.x);
                    f.homeY = static_cast<float>(std::max(7, FloorYAt(anem.x) - 5));
                    f.x = Clamp(f.homeX + RandFloat(-8.0f, 8.0f), 8.0f, static_cast<float>(cols_ - 10));
                    f.y = f.homeY + RandFloat(-2.5f, 1.5f);
                }
                clownIdx++;
                break;
            case FishSpecies::Tropical:
                f.baseSpeed = RandFloat(6.2f, 9.2f);
                f.colorIndex = RandInt(0, 4);
                f.variant = RandInt(0, 2);
                break;
            case FishSpecies::Puffer:
                f.baseSpeed = RandFloat(3.8f, 5.4f);
                f.colorIndex = 2;
                f.variant = 0;
                f.depthLayer = 1;
                break;
            case FishSpecies::Angel:
                f.baseSpeed = RandFloat(3.6f, 5.2f);
                f.colorIndex = RandInt(1, 4);
                f.variant = 0;
                f.depthLayer = RandInt(1, 2);
                break;
            case FishSpecies::ReefBig:
                f.baseSpeed = RandFloat(4.2f, 6.0f);
                f.colorIndex = RandInt(0, 3);
                f.variant = depthBand % 2; // 0: curvy koi/tang, 1: sleek torpedo reef fish
                f.depthLayer = (f.variant == 0) ? 2 : 1;
                break;
        }

        // Far-layer fish swim slightly more slowly for atmospheric parallax
        if (f.depthLayer == 0) f.baseSpeed *= 0.82f;
        else if (f.depthLayer == 2) f.baseSpeed *= 1.05f;

        f.vx = f.baseSpeed * f.facing * 0.85f;
        f.vy = RandFloat(-0.35f, 0.35f);
        f.phase = RandFloat(0.0f, 2.0f * PI);
        f.burstTimer = RandFloat(0.4f, 2.2f);
        f.burstAmount = RandFloat(0.0f, 0.4f);
        f.pauseTimer = 0.0f;
        f.scaredTimer = 0.0f;
        f.hungryCooldown = 0.0f;
        f.swimTargetY = f.y;
        f.turnTimer = RandFloat(5.0f, 14.0f);
        f.turnCooldown = 0.0f;
        fishes_.push_back(f);
    };

    for (int i = 0; i < minnowCount; ++i)   addFish(FishSpecies::Minnow);
    for (int i = 0; i < clownCount; ++i)    addFish(FishSpecies::Clownfish);
    for (int i = 0; i < tropicalCount; ++i) addFish(FishSpecies::Tropical);
    for (int i = 0; i < pufferCount; ++i)   addFish(FishSpecies::Puffer);
    for (int i = 0; i < angelCount; ++i)    addFish(FishSpecies::Angel);
    for (int i = 0; i < reefBigCount; ++i)  addFish(FishSpecies::ReefBig);

    // Sleek slow-gliding Sea Turtle in the upper-middle water
    turtle_.x = cols_ * 0.28f;
    turtle_.y = static_cast<float>(std::max(8, vb / 4));
    turtle_.targetY = turtle_.y;
    turtle_.facing = 1;
    turtle_.vx = 2.8f;
    turtle_.vy = 0.0f;
    turtle_.phase = 0.0f;

    // Delicate bobbing Seahorses near seaweed
    seahorses_.clear();
    int shCount = (cols_ >= 120) ? 2 : 1;
    for (int i = 0; i < shCount; ++i) {
        Seahorse sh{};
        sh.baseX = cols_ * (i == 0 ? 0.13f : 0.87f);
        sh.baseY = static_cast<float>(std::max(10, FloorYAt(static_cast<int>(sh.baseX)) - 9 - i * 2));
        sh.x = sh.baseX;
        sh.y = sh.baseY;
        sh.phase = RandFloat(0.0f, 2.0f * PI);
        sh.facing = (i == 0) ? 1 : -1;
        sh.colorIndex = (i == 0) ? 2 : 3;
        seahorses_.push_back(sh);
    }

    // Translucent pulsating Jellyfish with flowing tentacles
    jellies_.clear();
    int jellyCount = (cols_ >= 130) ? 3 : 2;
    for (int i = 0; i < jellyCount; ++i) {
        Jellyfish j{};
        j.x = cols_ * (0.18f + i * 0.31f) + RandFloat(-5.0f, 5.0f);
        j.y = RandFloat(7.0f, static_cast<float>(std::max(13, vb - 16)));
        j.vx = RandFloat(-0.9f, 0.9f);
        j.vy = 0.0f;
        j.phase = RandFloat(0.0f, 2.0f * PI);
        j.pulseSpeed = RandFloat(1.15f, 1.55f);
        j.trailOffset = 0.0f;
        jellies_.push_back(j);
    }

    // Detailed 3-row Crab on the sand floor
    crab_.x = static_cast<float>(cols_) * 0.42f;
    crab_.vx = 2.8f;
    crab_.pauseTimer = 1.5f;
    crab_.animPhase = 0.0f;
    crab_.startledTimer = 0.0f;

    bubbles_.clear();
    for (int i = 0; i < 16; ++i) {
        SpawnBubble(RandFloat(3.0f, static_cast<float>(cols_ - 4)),
                    RandFloat(6.0f, static_cast<float>(vb - 5)));
    }
    swirls_.clear();
    for (int i = 0; i < 3; ++i) {
        SpawnSwirl(RandFloat(cols_ * 0.12f, cols_ * 0.88f),
                   RandFloat(7.5f, static_cast<float>(std::max(12, vb - 11))),
                   RandFloat(2.8f, 4.4f), RandFloat(0.65f, 1.0f));
    }

    // Rare majestic Great Shark: spawns randomly from left or right once every 30 to 60 minutes
    shark_.active = false;
    shark_.nextSpawnTimer = RandFloat(1800.0f, 3600.0f);
    shark_.wakeTimer = 0.0f;

    // Rare quirky Submarine: spawns randomly from left or right once every 20 to 45 minutes
    sub_.state = SubmarineState::Inactive;
    sub_.nextSpawnTimer = RandFloat(1200.0f, 2700.0f);
    sub_.periscopeExt = 0.0f;

    foods_.clear();
    ripples_.clear();
    sparkles_.clear();
    surfaceDisturbAmp_ = 0.0f;
}

void Aquarium::SpawnSwirl(float x, float y, float radius, float strength, float vx) {
    if (swirls_.size() >= 8) return;
    WaterSwirl sw{};
    sw.x = x;
    sw.y = y;
    sw.vx = (std::fabs(vx) > 0.01f) ? vx : RandFloat(-0.9f, 0.9f);
    sw.vy = RandFloat(-0.22f, 0.22f);
    sw.radius = (radius > 0.1f) ? radius : RandFloat(2.6f, 4.5f);
    sw.angle = RandFloat(0.0f, 2.0f * PI);
    sw.spinSpeed = ((RandInt(0, 1) == 0) ? 1.0f : -1.0f) * RandFloat(1.35f, 2.35f);
    sw.age = 0.0f;
    sw.maxLife = RandFloat(6.5f, 12.5f);
    sw.strength = Clamp(strength, 0.35f, 1.35f);
    swirls_.push_back(sw);
}

void Aquarium::SampleSwirlFlow(float x, float y, float& outVx, float& outVy) const {
    outVx = 0.0f;
    outVy = 0.0f;
    for (const auto& sw : swirls_) {
        float lifeFade = 1.0f;
        if (sw.age < 1.2f) lifeFade = sw.age / 1.2f;
        else if (sw.age > sw.maxLife - 1.8f) lifeFade = std::max(0.0f, (sw.maxLife - sw.age) / 1.8f);

        float dx = x - sw.x;
        float dy = (y - sw.y) * 1.9f;
        float d2 = dx * dx + dy * dy;
        float maxR = sw.radius * 2.4f;
        if (d2 < maxR * maxR && d2 > 0.15f) {
            float d = std::sqrt(d2);
            float falloff = (1.0f - d / maxR) * lifeFade * sw.strength;
            float dir = (sw.spinSpeed >= 0.0f) ? 1.0f : -1.0f;
            // Tangential vortex swirl + slight inward curl
            outVx += (-dy / d * dir * 2.4f - dx / d * 0.25f) * falloff;
            outVy += ( dx / d * dir * 1.1f - dy / d * 0.12f) * falloff;
        }
    }
}

void Aquarium::SummonShark() {
    const int vb = VisibleBottom();
    shark_.active = true;
    shark_.facing = (RandInt(0, 1) == 0) ? 1 : -1;
    shark_.x = (shark_.facing > 0) ? -50.0f : static_cast<float>(cols_ + 6);
    float minY = 9.0f;
    float maxY = static_cast<float>(std::max(14, vb - 16));
    shark_.y = RandFloat(minY, maxY);
    shark_.targetY = Clamp(shark_.y + RandFloat(-3.0f, 3.0f), minY, maxY);
    shark_.vx = shark_.facing * RandFloat(2.45f, 3.05f);
    shark_.vy = 0.0f;
    shark_.phase = RandFloat(0.0f, 2.0f * PI);
    shark_.wakeTimer = 0.4f;
}

void Aquarium::SummonSubmarine() {
    const int vb = VisibleBottom();
    sub_.state = SubmarineState::CruisingIn;
    sub_.facing = (RandInt(0, 1) == 0) ? 1 : -1;
    sub_.x = (sub_.facing > 0) ? -25.0f : static_cast<float>(cols_ + 4);
    float minCruise = 11.0f;
    float maxCruise = static_cast<float>(std::max(14, vb - 14));
    sub_.cruiseY = RandFloat(minCruise, maxCruise);
    sub_.y = sub_.cruiseY;
    sub_.vx = sub_.facing * RandFloat(3.4f, 4.1f);
    sub_.vy = 0.0f;
    sub_.stopX = RandFloat(cols_ * 0.28f, cols_ * 0.62f);
    sub_.propPhase = 0.0f;
    sub_.periscopeExt = 0.0f;
    sub_.stateTimer = 0.0f;
    sub_.lookDir = sub_.facing;
}

void Aquarium::SpawnBubble(float x, float y, int size, bool golden) {
    if (bubbles_.size() > 105) return;
    Bubble b{};
    b.x = x;
    b.y = y;
    b.size = (size >= 0) ? size : RandInt(0, 2);
    b.vy = -RandFloat(3.2f, 5.8f) - b.size * 0.65f;
    b.phase = RandFloat(0.0f, 2.0f * PI);
    b.growTimer = RandFloat(2.2f, 4.5f);
    b.popping = false;
    b.popTimer = 0.0f;
    b.golden = golden;
    bubbles_.push_back(b);
}

void Aquarium::OnMouseMove(float col, float row, bool active) {
    mouseActive_ = active;
    if (!active) {
        mouseVx_ *= 0.82f;
        mouseVy_ *= 0.82f;
        mouseSpeed_ *= 0.82f;
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
    mouseVx_ = mouseVx_ * 0.55f + dx * 0.45f;
    mouseVy_ = mouseVy_ * 0.55f + dy * 0.45f;
    mouseSpeed_ = std::sqrt(mouseVx_ * mouseVx_ + mouseVy_ * mouseVy_);
    mouseX_ = col;
    mouseY_ = row;

    int vb = VisibleBottom();
    if (row < 7.0f && mouseSpeed_ > 1.2f) {
        surfaceDisturbX_ = col;
        surfaceDisturbAmp_ = Clamp(surfaceDisturbAmp_ + mouseSpeed_ * 0.14f, 0.0f, 1.35f);
    }
    if (mouseSpeed_ > 3.2f && row > 3.0f && row < vb - 3.0f && RandInt(0, 3) == 0) {
        SpawnBubble(col + RandFloat(-1.0f, 1.0f), row + RandFloat(-0.5f, 0.5f), RandInt(0, 1));
    }
    if (mouseSpeed_ > 4.2f && row > 6.0f && row < vb - 7.0f && RandInt(0, 8) == 0) {
        SpawnSwirl(col, row, RandFloat(2.4f, 3.6f), 0.95f, Clamp(mouseVx_ * 0.25f, -1.5f, 1.5f));
    }
}

void Aquarium::DropFood(float col, float row, int count) {
    int vb = VisibleBottom();
    float cx = Clamp(col, 2.0f, static_cast<float>(cols_ - 3));
    float cy = Clamp(row, 3.5f, static_cast<float>(vb - 6));

    Ripple rp{};
    rp.x = cx;
    rp.y = cy;
    rp.radius = 0.8f;
    rp.maxRadius = 8.5f;
    rp.life = 1.0f;
    ripples_.push_back(rp);

    if (cy < 8.0f) {
        surfaceDisturbX_ = cx;
        surfaceDisturbAmp_ = 1.15f;
    }

    const wchar_t pellets[] = { L'\x2022', L'\x00B7', L'\x00B0', L'*' };
    for (int i = 0; i < count; ++i) {
        if (foods_.size() >= 65) foods_.erase(foods_.begin());
        FoodPellet fp{};
        fp.x = Clamp(cx + RandFloat(-3.2f, 3.2f), 2.0f, static_cast<float>(cols_ - 3));
        fp.y = Clamp(cy + RandFloat(-0.8f, 0.8f), 3.5f, static_cast<float>(vb - 5));
        fp.vx = RandFloat(-0.8f, 0.8f);
        fp.vy = RandFloat(1.6f, 2.9f);
        fp.phase = RandFloat(0.0f, 2.0f * PI);
        fp.life = RandFloat(20.0f, 30.0f);
        fp.ch = pellets[RandInt(0, 3)];
        foods_.push_back(fp);
    }

    for (int i = 0; i < 3; ++i) {
        SpawnBubble(cx + RandFloat(-1.5f, 1.5f), cy, RandInt(0, 1));
    }
}

void Aquarium::DropFoodFromSurface(int count) {
    float cx = RandFloat(cols_ * 0.2f, cols_ * 0.8f);
    DropFood(cx, 4.0f, count);
}

void Aquarium::Update(float dt) {
    if (cols_ <= 0 || rows_ <= 0) return;
    dt = Clamp(dt, 0.001f, 0.08f);
    time_ += dt;

    surfaceDisturbAmp_ *= std::exp(-dt * 1.9f);

    UpdateSeaweed(dt);
    UpdateSwirls(dt);
    UpdateMarineSnow(dt);
    UpdateFood(dt);
    UpdateShark(dt);
    UpdateSubmarine(dt);
    UpdateFish(dt);
    UpdateTurtle(dt);
    UpdateSeahorses(dt);
    UpdateJellyfish(dt);
    UpdateCrab(dt);
    UpdateChest(dt);
    UpdateBubbles(dt);
    UpdateRipples(dt);
    UpdateSparkles(dt);

    mouseVx_ *= std::exp(-dt * 5.0f);
    mouseVy_ *= std::exp(-dt * 5.0f);
    mouseSpeed_ *= std::exp(-dt * 5.0f);
}

void Aquarium::UpdateSeaweed(float dt) {
    for (auto& s : seaweeds_) {
        s.bend += (0.0f - s.bend) * std::min(1.0f, dt * 2.8f);

        if (mouseActive_) {
            int baseY = FloorYAt(s.baseX);
            int topY = baseY - s.height;
            float dx = static_cast<float>(s.baseX) - mouseX_;
            if (std::fabs(dx) < 11.0f && mouseY_ >= topY - 2 && mouseY_ <= baseY + 1) {
                float push = (mouseSpeed_ > 0.5f) ? Clamp(mouseVx_ * 0.5f, -3.8f, 3.8f)
                                                  : ((dx >= 0.0f) ? 1.1f : -1.1f);
                s.bend = Clamp(s.bend + push * dt * 5.5f, -3.8f, 3.8f);
            }
        }

        if (RandInt(0, 460) == 0) {
            int baseY = FloorYAt(s.baseX);
            SpawnBubble(static_cast<float>(s.baseX), static_cast<float>(baseY - s.height), 0);
        }
    }
}

void Aquarium::UpdateSwirls(float dt) {
    const int vb = VisibleBottom();
    // Maintain 3..5 gentle organic underwater swirl eddies
    if (swirls_.size() < 4 && RandInt(0, 110) == 0) {
        SpawnSwirl(RandFloat(cols_ * 0.08f, cols_ * 0.92f),
                   RandFloat(6.5f, static_cast<float>(std::max(12, vb - 11))),
                   RandFloat(2.5f, 4.4f), RandFloat(0.60f, 1.05f));
    }

    for (auto it = swirls_.begin(); it != swirls_.end();) {
        it->age += dt;
        if (it->age >= it->maxLife) {
            it = swirls_.erase(it);
            continue;
        }
        float cur = CurrentXAt(it->x, it->y);
        it->vx += (cur * 0.85f - it->vx) * std::min(1.0f, dt * 0.8f);
        it->x += it->vx * dt;
        it->y += (it->vy + std::sin(time_ * 0.8f + it->angle) * 0.12f) * dt;
        it->angle += it->spinSpeed * dt;

        // Slight expansion as eddy dissipates naturally
        it->radius = std::min(5.2f, it->radius + dt * 0.07f);

        float maxY = static_cast<float>(std::max(11, vb - 9));
        it->y = Clamp(it->y, 6.0f, maxY);
        if (it->x < -6.0f || it->x > cols_ + 6.0f) {
            it = swirls_.erase(it);
        } else {
            ++it;
        }
    }
}

void Aquarium::UpdateShark(float dt) {
    const int vb = VisibleBottom();
    if (!shark_.active) {
        shark_.nextSpawnTimer -= dt;
        if (shark_.nextSpawnTimer <= 0.0f) {
            // Avoid overlapping directly with an active submarine
            if (sub_.state != SubmarineState::Inactive) {
                shark_.nextSpawnTimer = 20.0f;
                return;
            }
            SummonShark();
        }
        return;
    }

    // Unhurried, powerful, majestic cruising motion
    shark_.phase += dt * 1.45f;
    float tailPulse = 0.88f + 0.22f * std::sin(shark_.phase);
    float cruiseSpeed = 2.65f * tailPulse;
    float targetVx = cruiseSpeed * shark_.facing + CurrentXAt(shark_.x + 22.0f, shark_.y) * 0.2f;
    float targetVy = Clamp((shark_.targetY - shark_.y) * 0.22f, -0.65f, 0.65f)
                   + std::sin(shark_.phase * 0.35f) * 0.22f;

    if (RandInt(0, 320) == 0) {
        shark_.targetY = RandFloat(9.0f, static_cast<float>(std::max(14, vb - 16)));
    }

    shark_.vx += (targetVx - shark_.vx) * std::min(1.0f, dt * 1.2f);
    shark_.vy += (targetVy - shark_.vy) * std::min(1.0f, dt * 1.2f);
    shark_.x += shark_.vx * dt;
    shark_.y += shark_.vy * dt;
    shark_.y = Clamp(shark_.y, 8.5f, static_cast<float>(std::max(13, vb - 15)));

    // Subtle swirling wake eddies & trailing bubbles behind the sweeping tail fin
    shark_.wakeTimer -= dt;
    float tailX = (shark_.facing > 0) ? (shark_.x + 2.0f) : (shark_.x + 44.0f);
    float tailY = shark_.y + 1.5f;
    if (shark_.wakeTimer <= 0.0f) {
        shark_.wakeTimer = RandFloat(2.4f, 4.2f);
        if (tailX > 2.0f && tailX < cols_ - 2.0f) {
            SpawnSwirl(tailX, tailY + RandFloat(-1.2f, 1.2f), RandFloat(2.6f, 3.8f),
                       0.95f, -shark_.facing * 0.7f);
        }
    }
    if (RandInt(0, 95) == 0 && tailX > 1.0f && tailX < cols_ - 1.0f) {
        SpawnBubble(tailX + RandFloat(-1.5f, 1.5f), tailY + RandFloat(-1.0f, 1.0f), RandInt(0, 1));
    }

    // Finish crossing -> schedule next random visit in 30 to 60 minutes (1800..3600 seconds)
    if ((shark_.facing > 0 && shark_.x > cols_ + 10.0f) ||
        (shark_.facing < 0 && shark_.x < -56.0f)) {
        shark_.active = false;
        shark_.nextSpawnTimer = RandFloat(1800.0f, 3600.0f);
    }
}

void Aquarium::UpdateSubmarine(float dt) {
    if (sub_.state == SubmarineState::Inactive) {
        sub_.nextSpawnTimer -= dt;
        if (sub_.nextSpawnTimer <= 0.0f) {
            if (shark_.active) {
                sub_.nextSpawnTimer = 20.0f;
                return;
            }
            SummonSubmarine();
        }
        return;
    }

    float propSpeed = (sub_.state == SubmarineState::PeriscopeUp ||
                       sub_.state == SubmarineState::LookingAround ||
                       sub_.state == SubmarineState::PeriscopeDown) ? 2.2f : 9.5f;
    sub_.propPhase += dt * propSpeed;

    float propX = (sub_.facing > 0) ? (sub_.x - 0.5f) : (sub_.x + 22.0f);
    float propY = sub_.y + 2.0f;
    float centerX = sub_.x + 11.0f;

    switch (sub_.state) {
        case SubmarineState::CruisingIn: {
            float targetVx = sub_.facing * 3.8f;
            float targetVy = std::sin(time_ * 1.1f) * 0.25f;
            sub_.vx += (targetVx - sub_.vx) * std::min(1.0f, dt * 2.0f);
            sub_.vy += (targetVy - sub_.vy) * std::min(1.0f, dt * 2.0f);
            sub_.x += sub_.vx * dt;
            sub_.y += sub_.vy * dt;

            if (RandInt(0, 14) == 0 && propX > 1.0f && propX < cols_ - 1.0f) {
                SpawnBubble(propX, propY + RandFloat(-0.5f, 0.5f), RandInt(0, 1));
            }
            if (RandInt(0, 140) == 0 && propX > 2.0f && propX < cols_ - 2.0f) {
                SpawnSwirl(propX, propY, RandFloat(2.0f, 3.0f), 0.75f, -sub_.facing * 0.6f);
            }

            bool reachedStop = (sub_.facing > 0) ? (sub_.x >= sub_.stopX) : (sub_.x <= sub_.stopX);
            if (reachedStop) {
                sub_.state = SubmarineState::Surfacing;
                sub_.stateTimer = 0.0f;
            }
            break;
        }

        case SubmarineState::Surfacing: {
            // Decelerate smoothly and ascend until the conning tower rises cleanly above the waterline
            float targetVx = sub_.facing * 0.9f;
            float targetVy = -2.35f;
            sub_.vx += (targetVx - sub_.vx) * std::min(1.0f, dt * 1.6f);
            sub_.vy += (targetVy - sub_.vy) * std::min(1.0f, dt * 2.2f);
            sub_.x += sub_.vx * dt;
            sub_.y += sub_.vy * dt;

            // Ballast tank bubbles streaming upward while surfacing
            if (RandInt(0, 8) == 0) {
                SpawnBubble(sub_.x + RandFloat(3.0f, 18.0f), sub_.y + 1.0f, RandInt(0, 2));
            }

            if (sub_.y <= 2.15f) {
                sub_.y = 2.0f;
                sub_.vy = 0.0f;
                sub_.state = SubmarineState::PeriscopeUp;
                sub_.stateTimer = 0.0f;
                // Surface splash & wave swell as submarine breaks through the water!
                surfaceDisturbX_ = centerX;
                surfaceDisturbAmp_ = 1.25f;
                Ripple rp{ centerX, 3.0f, 1.5f, 10.0f, 1.0f };
                ripples_.push_back(rp);
            }
            break;
        }

        case SubmarineState::PeriscopeUp: {
            sub_.vx *= std::exp(-dt * 2.6f);
            sub_.x += sub_.vx * dt;
            sub_.y = 2.0f + SurfaceWaveAt(centerX) * 0.18f;
            sub_.periscopeExt = std::min(1.0f, sub_.periscopeExt + dt * 0.85f);
            sub_.lookDir = sub_.facing;

            if (sub_.periscopeExt >= 1.0f) {
                sub_.state = SubmarineState::LookingAround;
                sub_.stateTimer = 0.0f;
            }
            break;
        }

        case SubmarineState::LookingAround: {
            sub_.vx *= std::exp(-dt * 3.0f);
            sub_.x += sub_.vx * dt;
            sub_.y = 2.0f + SurfaceWaveAt(centerX) * 0.18f;
            sub_.stateTimer += dt;

            // Smoothly look forward -> center -> backward -> center -> forward
            if (sub_.stateTimer < 1.3f) {
                sub_.lookDir = sub_.facing;
            } else if (sub_.stateTimer < 2.2f) {
                sub_.lookDir = 0;
            } else if (sub_.stateTimer < 3.8f) {
                sub_.lookDir = -sub_.facing;
            } else if (sub_.stateTimer < 4.6f) {
                sub_.lookDir = 0;
            } else {
                sub_.lookDir = sub_.facing;
            }

            // Gentle surface wave lapping around the surfaced conning tower
            if (RandInt(0, 40) == 0) {
                surfaceDisturbX_ = centerX;
                surfaceDisturbAmp_ = std::max(surfaceDisturbAmp_, 0.45f);
            }

            if (sub_.stateTimer >= 5.6f) {
                sub_.state = SubmarineState::PeriscopeDown;
                sub_.stateTimer = 0.0f;
            }
            break;
        }

        case SubmarineState::PeriscopeDown: {
            sub_.y = 2.0f + SurfaceWaveAt(centerX) * 0.18f;
            sub_.periscopeExt = std::max(0.0f, sub_.periscopeExt - dt * 0.95f);

            if (sub_.periscopeExt <= 0.0f) {
                sub_.state = SubmarineState::Diving;
                sub_.stateTimer = 0.0f;
                // Vent air bubbles as ballast tanks flood for diving!
                for (int i = 0; i < 6; ++i) {
                    SpawnBubble(sub_.x + RandFloat(3.0f, 18.0f), 2.8f, RandInt(0, 2));
                }
                surfaceDisturbX_ = centerX;
                surfaceDisturbAmp_ = 1.1f;
            }
            break;
        }

        case SubmarineState::Diving: {
            float targetVx = sub_.facing * 3.8f;
            float targetVy = 2.1f;
            sub_.vx += (targetVx - sub_.vx) * std::min(1.0f, dt * 1.5f);
            sub_.vy += (targetVy - sub_.vy) * std::min(1.0f, dt * 1.8f);
            sub_.x += sub_.vx * dt;
            sub_.y += sub_.vy * dt;

            if (RandInt(0, 9) == 0) {
                SpawnBubble(sub_.x + RandFloat(2.0f, 19.0f), sub_.y + 1.0f, RandInt(0, 2));
            }
            if (RandInt(0, 14) == 0 && propX > 1.0f && propX < cols_ - 1.0f) {
                SpawnBubble(propX, propY, 0);
            }

            if (sub_.y >= sub_.cruiseY) {
                sub_.y = sub_.cruiseY;
                sub_.state = SubmarineState::CruisingOut;
                sub_.stateTimer = 0.0f;
            }
            break;
        }

        case SubmarineState::CruisingOut: {
            float targetVx = sub_.facing * 4.0f;
            float targetVy = std::sin(time_ * 1.1f) * 0.25f;
            sub_.vx += (targetVx - sub_.vx) * std::min(1.0f, dt * 1.8f);
            sub_.vy += (targetVy - sub_.vy) * std::min(1.0f, dt * 2.0f);
            sub_.x += sub_.vx * dt;
            sub_.y += sub_.vy * dt;

            if (RandInt(0, 14) == 0 && propX > 1.0f && propX < cols_ - 1.0f) {
                SpawnBubble(propX, propY + RandFloat(-0.5f, 0.5f), RandInt(0, 1));
            }
            if (RandInt(0, 130) == 0 && propX > 2.0f && propX < cols_ - 2.0f) {
                SpawnSwirl(propX, propY, RandFloat(2.0f, 3.0f), 0.75f, -sub_.facing * 0.6f);
            }

            if ((sub_.facing > 0 && sub_.x > cols_ + 8.0f) ||
                (sub_.facing < 0 && sub_.x < -30.0f)) {
                sub_.state = SubmarineState::Inactive;
                sub_.nextSpawnTimer = RandFloat(1200.0f, 2700.0f);
            }
            break;
        }

        case SubmarineState::Inactive:
            break;
    }
}

void Aquarium::UpdateMarineSnow(float dt) {
    const int vb = VisibleBottom();
    for (auto& ms : specks_) {
        float cur = CurrentXAt(ms.x, ms.y);
        float svx = 0.0f, svy = 0.0f;
        SampleSwirlFlow(ms.x, ms.y, svx, svy);
        ms.x += (cur * 0.55f + ms.vx + svx * 0.65f + std::sin(time_ * 0.9f + ms.phase) * 0.25f) * dt;
        ms.y += (ms.vy + svy * 0.55f + std::cos(time_ * 0.6f + ms.phase) * 0.12f) * dt;

        if (ms.x < 1.0f) ms.x = static_cast<float>(cols_ - 2);
        else if (ms.x > cols_ - 2.0f) ms.x = 1.0f;

        int fy = FloorYAt(static_cast<int>(ms.x));
        if (ms.y >= fy - 1.5f || ms.y < 4.5f) {
            ms.y = RandFloat(5.0f, 9.0f);
            ms.x = RandFloat(2.0f, static_cast<float>(cols_ - 3));
        }
        (void)vb;
    }
}

void Aquarium::UpdateFood(float dt) {
    for (auto it = foods_.begin(); it != foods_.end();) {
        it->life -= dt;
        float cur = CurrentXAt(it->x, it->y);
        it->vx += (cur * 0.65f - it->vx) * std::min(1.0f, dt * 2.5f);
        it->y += it->vy * dt;
        it->x += (it->vx + std::sin(time_ * 2.3f + it->phase) * 0.75f) * dt;
        it->x = Clamp(it->x, 1.0f, static_cast<float>(cols_ - 2));

        float fy = FloorHeightFAt(it->x);
        if (it->y >= fy - 0.6f) {
            it->y = fy - 0.6f;
            it->vy = 0.0f;
            it->vx *= 0.6f;
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
    const float topBound = 5.2f;
    const int vb = VisibleBottom();

    // Shared gentle wandering depth for minnow shoals
    float shoalAnchorY = 9.0f + (std::sin(time_ * 0.19f) * 0.5f + 0.5f) * static_cast<float>(std::max(6, vb - 20));

    for (size_t i = 0; i < fishes_.size(); ++i) {
        auto& f = fishes_[i];
        if (f.scaredTimer > 0.0f) f.scaredTimer = std::max(0.0f, f.scaredTimer - dt);
        if (f.hungryCooldown > 0.0f) f.hungryCooldown = std::max(0.0f, f.hungryCooldown - dt);
        if (f.turnCooldown > 0.0f) f.turnCooldown = std::max(0.0f, f.turnCooldown - dt);
        if (f.pauseTimer > 0.0f) f.pauseTimer = std::max(0.0f, f.pauseTimer - dt);
        f.turnTimer -= dt;
        f.burstTimer -= dt;

        // Burst-and-glide decay (authentic water drag)
        f.burstAmount *= std::exp(-dt * 2.4f);

        // Trigger periodic natural tail-kick bursts
        if (f.burstTimer <= 0.0f && f.pauseTimer <= 0.0f) {
            switch (f.species) {
                case FishSpecies::Minnow:
                    f.burstTimer = RandFloat(0.65f, 1.35f);
                    f.burstAmount = RandFloat(0.65f, 1.0f);
                    break;
                case FishSpecies::Clownfish:
                    f.burstTimer = RandFloat(0.85f, 1.75f);
                    f.burstAmount = RandFloat(0.60f, 0.95f);
                    break;
                case FishSpecies::Tropical:
                    f.burstTimer = RandFloat(1.1f, 2.3f);
                    f.burstAmount = RandFloat(0.55f, 0.95f);
                    break;
                case FishSpecies::Puffer:
                    f.burstTimer = RandFloat(1.3f, 2.6f);
                    f.burstAmount = RandFloat(0.35f, 0.65f);
                    break;
                case FishSpecies::Angel:
                    f.burstTimer = RandFloat(1.9f, 3.6f);
                    f.burstAmount = RandFloat(0.45f, 0.80f);
                    break;
                case FishSpecies::ReefBig:
                    f.burstTimer = RandFloat(1.7f, 3.2f);
                    f.burstAmount = RandFloat(0.55f, 0.90f);
                    break;
            }
        }

        // 1. Seek nearest food pellet
        int targetFood = -1;
        float bestFoodDist = (f.species == FishSpecies::Minnow) ? 34.0f : 50.0f;
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

        float currentX = CurrentXAt(f.x, f.y);

        if (targetFood >= 0) {
            f.pauseTimer = 0.0f;
            const auto& food = foods_[targetFood];
            float dx = food.x - f.x;
            float dy = food.y - f.y;
            float dist = std::sqrt(dx * dx + dy * dy) + 0.01f;
            float chaseSpeed = f.baseSpeed * (1.55f + 0.25f * f.burstAmount);
            float desiredVx = (dx / dist) * chaseSpeed;
            float desiredVy = (dy / dist) * (chaseSpeed * 0.52f);
            f.desiredFacing = (dx >= 0.0f) ? 1 : -1;
            f.vx += (desiredVx - f.vx) * std::min(1.0f, dt * 4.2f);
            f.vy += (desiredVy - f.vy) * std::min(1.0f, dt * 4.2f);

            if (dist < 2.5f) {
                Sparkle sp{ food.x, food.y, 0.45f, L'*' };
                sparkles_.push_back(sp);
                SpawnBubble(f.x, f.y - 0.5f, 0);
                foods_.erase(foods_.begin() + targetFood);
                f.hungryCooldown = 0.65f;
                f.burstAmount = 0.8f;
            }
        } else {
            // Periodic natural decision: change depth, smooth U-turn, or hover briefly
            if (f.turnTimer <= 0.0f) {
                f.turnTimer = (f.species == FishSpecies::Angel || f.species == FishSpecies::Puffer)
                            ? RandFloat(4.5f, 10.5f)
                            : RandFloat(5.5f, 13.5f);

                // Smooth turning (avoid rapid jitter)
                if (f.turnCooldown <= 0.0f && RandInt(0, 2) == 0) {
                    f.desiredFacing = -f.desiredFacing;
                    f.turnCooldown = 2.6f;
                }

                // Natural brief "hover & inspect" pause for slower reef species
                if (f.scaredTimer <= 0.0f &&
                    (f.species == FishSpecies::Angel || f.species == FishSpecies::Puffer ||
                     f.species == FishSpecies::Clownfish || f.species == FishSpecies::ReefBig) &&
                    RandInt(0, 3) == 0) {
                    f.pauseTimer = RandFloat(1.0f, 2.6f);
                }

                int maxY = std::max(10, vb - 11);
                if (f.species == FishSpecies::Clownfish) {
                    f.swimTargetY = Clamp(f.homeY + RandFloat(-5.5f, 2.5f), 6.5f, static_cast<float>(maxY));
                } else {
                    f.swimTargetY = RandFloat(6.0f, static_cast<float>(maxY));
                }
            }

            // Natural edge turn-back for ~55% of encounters so tank feels alive & natural
            if (f.turnCooldown <= 0.0f && f.scaredTimer <= 0.0f) {
                if (f.x < 9.0f && f.desiredFacing < 0 && RandInt(0, 100) < 55) {
                    f.desiredFacing = 1;
                    f.turnCooldown = 3.2f;
                } else if (f.x > cols_ - 9.0f && f.desiredFacing > 0 && RandInt(0, 100) < 55) {
                    f.desiredFacing = -1;
                    f.turnCooldown = 3.2f;
                }
            }

            // Species-specific cruise speed modulation (burst-and-glide + hover)
            float glideFactor = (f.pauseTimer > 0.0f)
                              ? 0.14f
                              : (0.56f + 0.62f * f.burstAmount);
            float targetVx = f.baseSpeed * glideFactor * f.desiredFacing + currentX * 0.45f;

            // Gentle vertical wandering & subtle hover bobbing
            float bobAmp = (f.species == FishSpecies::Clownfish) ? 0.95f
                         : (f.species == FishSpecies::Puffer)    ? 0.75f
                         : (f.species == FishSpecies::Angel)     ? 0.45f
                                                                 : 0.55f;
            float targetVy = Clamp((f.swimTargetY - f.y) * 0.34f, -1.85f, 1.85f)
                           + std::sin(f.phase * 0.42f) * bobAmp;

            // Clownfish softly orbit their home coral / sea anemone
            if (f.species == FishSpecies::Clownfish && f.scaredTimer <= 0.0f) {
                float distHome = f.homeX - f.x;
                if (std::fabs(distHome) > 26.0f && f.turnCooldown <= 0.0f) {
                    f.desiredFacing = (distHome >= 0.0f) ? 1 : -1;
                    f.turnCooldown = 2.4f;
                }
                targetVy += Clamp((f.homeY - f.y) * 0.12f, -0.6f, 0.6f);
            }

            // Full Boids schooling for Minnows (separation + alignment + cohesion)
            if (f.species == FishSpecies::Minnow && f.scaredTimer <= 0.0f) {
                float avgX = 0.0f, avgY = 0.0f, avgVx = 0.0f, avgVy = 0.0f;
                float sepX = 0.0f, sepY = 0.0f;
                int neighbors = 0;
                for (size_t j = 0; j < fishes_.size(); ++j) {
                    if (i == j || fishes_[j].species != FishSpecies::Minnow) continue;
                    float dx = fishes_[j].x - f.x;
                    float dy = fishes_[j].y - f.y;
                    float d2 = dx * dx + dy * dy * 2.8f;
                    if (d2 < 380.0f) {
                        avgX += fishes_[j].x;
                        avgY += fishes_[j].y;
                        avgVx += fishes_[j].vx;
                        avgVy += fishes_[j].vy;
                        neighbors++;
                        if (d2 < 26.0f && d2 > 0.01f) {
                            float inv = 1.0f / std::sqrt(d2);
                            sepX -= dx * inv * 2.2f;
                            sepY -= dy * inv * 1.4f;
                        }
                    }
                }
                if (neighbors > 0) {
                    avgX /= neighbors;
                    avgY /= neighbors;
                    avgVx /= neighbors;
                    avgVy /= neighbors;
                    // Align & cohere smoothly
                    targetVx = targetVx * 0.58f + avgVx * 0.34f + Clamp((avgX - f.x) * 0.18f, -2.5f, 2.5f) + sepX;
                    targetVy = targetVy * 0.55f + avgVy * 0.20f + Clamp((avgY - f.y) * 0.25f, -1.6f, 1.6f) + sepY;
                    targetVy += Clamp((shoalAnchorY - f.y) * 0.08f, -0.5f, 0.5f);
                    if (std::fabs(avgVx) > 2.5f) {
                        f.desiredFacing = (avgVx > 0.0f) ? 1 : -1;
                    }
                }
            } else if (f.scaredTimer <= 0.0f) {
                // Gentle personal space separation for larger fish so they don't awkwardly stack
                for (size_t j = 0; j < fishes_.size(); ++j) {
                    if (i == j || fishes_[j].depthLayer != f.depthLayer) continue;
                    if (fishes_[j].species == FishSpecies::Minnow) continue;
                    float dx = f.x - fishes_[j].x;
                    float dy = (f.y - fishes_[j].y) * 1.9f;
                    float d2 = dx * dx + dy * dy;
                    if (d2 < 72.0f && d2 > 0.05f) {
                        float d = std::sqrt(d2);
                        targetVx += (dx / d) * 1.6f;
                        targetVy += ((f.y - fishes_[j].y) / d) * 1.0f;
                    }
                }
            }

            // Smooth look-ahead seabed contour & surface steering (no harsh bouncing!)
            float lookX = f.x + f.facing * 7.0f;
            float aheadFloor = FloorHeightFAt(lookX);
            float clearance = (f.species == FishSpecies::Angel || f.species == FishSpecies::ReefBig) ? 6.5f : 4.5f;
            if (f.y > aheadFloor - clearance) {
                float push = (f.y - (aheadFloor - clearance)) * 1.35f;
                targetVy -= push;
            }
            if (f.y < topBound + 2.8f) {
                targetVy += ((topBound + 2.8f) - f.y) * 1.1f;
            }

            // Slower turn inertia makes U-turns look silky and organic
            bool turning = (f.facing != f.desiredFacing);
            float relaxX = (f.scaredTimer > 0.0f) ? 1.5f : (turning ? 1.95f : 2.35f);
            float relaxY = (f.scaredTimer > 0.0f) ? 1.8f : 2.6f;
            if (turning) {
                // Subtle arc dive/rise while banking into a turn
                targetVy += std::sin(f.phase * 0.7f) * 0.55f;
            }
            f.vx += (targetVx - f.vx) * std::min(1.0f, dt * relaxX);
            f.vy += (targetVy - f.vy) * std::min(1.0f, dt * relaxY);
        }

        // Instinctive avoidance when the Great Shark glides nearby (fishes part smoothly!)
        if (shark_.active) {
            float sharkCenterX = shark_.x + (shark_.facing > 0 ? 30.0f : 16.0f);
            float sharkCenterY = shark_.y + 1.0f;
            float sdx = f.x - sharkCenterX;
            float sdy = (f.y - sharkCenterY) * 1.9f;
            float sdist = std::sqrt(sdx * sdx + sdy * sdy);
            if (sdist < 30.0f && sdist > 0.1f) {
                float flee = 1.0f - sdist / 30.0f;
                f.pauseTimer = 0.0f;
                f.burstAmount = std::max(f.burstAmount, 0.85f);
                f.vx += (sdx / sdist) * f.baseSpeed * (1.8f + flee * 1.6f) * dt * 4.5f;
                f.vy += ((f.y - sharkCenterY) / (sdist + 0.1f)) * 9.0f * (0.6f + flee) * dt * 3.5f;
                f.desiredFacing = (f.vx >= 0.0f) ? 1 : -1;
                f.scaredTimer = std::max(f.scaredTimer,
                    (f.species == FishSpecies::Puffer) ? 2.4f : 0.75f);
            }
        }

        // Respectful clearance around the humming submarine hull
        if (sub_.state != SubmarineState::Inactive) {
            float subCx = sub_.x + 11.0f;
            float subCy = sub_.y + 2.0f;
            float sdx = f.x - subCx;
            float sdy = (f.y - subCy) * 1.9f;
            float sdist = std::sqrt(sdx * sdx + sdy * sdy);
            if (sdist < 19.0f && sdist > 0.1f) {
                float push = 1.0f - sdist / 19.0f;
                f.vx += (sdx / sdist) * f.baseSpeed * (1.1f + push) * dt * 3.0f;
                f.vy += ((f.y - subCy) / (sdist + 0.1f)) * 6.0f * dt * 2.5f;
            }
        }

        // Mouse cursor interaction: fast sweep = startle + school panic; gentle hover = curiosity!
        if (mouseActive_) {
            float dx = f.x - mouseX_;
            float dy = (f.y - mouseY_) * 1.85f;
            float dist = std::sqrt(dx * dx + dy * dy);
            float reactRadius = (mouseSpeed_ > 2.2f) ? 23.0f : 15.0f;

            if (dist < reactRadius && dist > 0.05f) {
                float nx = dx / dist;
                float ny = (f.y - mouseY_) / (dist + 0.01f);
                float strength = 1.0f - dist / reactRadius;

                if (mouseSpeed_ > 1.75f || dist < 6.5f) {
                    f.pauseTimer = 0.0f;
                    f.burstAmount = 1.0f;
                    float burst = f.baseSpeed * (2.1f + strength * 1.45f);
                    f.vx += nx * burst * dt * 7.2f;
                    f.vy += ny * 9.5f * dt * 4.2f;
                    f.desiredFacing = (f.vx >= 0.0f) ? 1 : -1;
                    if (f.scaredTimer <= 0.1f && RandInt(0, 2) == 0) {
                        SpawnBubble(f.x, f.y, RandInt(0, 1));
                    }
                    float scareDur = (f.species == FishSpecies::Puffer) ? 2.8f : 1.05f;
                    f.scaredTimer = std::max(f.scaredTimer, scareDur);

                    // Propagate mild startle to immediate nearby fish (organic school flash!)
                    for (size_t j = 0; j < fishes_.size(); ++j) {
                        if (i == j || fishes_[j].scaredTimer > 0.25f) continue;
                        float sdx = fishes_[j].x - f.x;
                        float sdy = (fishes_[j].y - f.y) * 1.8f;
                        if (sdx * sdx + sdy * sdy < 130.0f) {
                            fishes_[j].scaredTimer = 0.55f;
                            fishes_[j].burstAmount = 0.85f;
                            fishes_[j].vx += nx * fishes_[j].baseSpeed * 0.45f;
                        }
                    }
                } else if (mouseSpeed_ < 0.65f && dist > 8.5f &&
                           (f.species == FishSpecies::Clownfish || f.species == FishSpecies::Tropical) &&
                           f.scaredTimer <= 0.0f) {
                    // Curious gentle drift toward a calm cursor
                    f.vx -= nx * f.baseSpeed * 0.28f * dt * 2.0f;
                    f.vy -= ny * 2.2f * dt * 1.6f;
                } else {
                    f.vx += nx * f.baseSpeed * strength * dt * 2.6f;
                    f.vy += ny * 3.8f * strength * dt * 2.2f;
                }
            }
        }

        float maxSpd = f.baseSpeed * 2.7f;
        f.vx = Clamp(f.vx, -maxSpd, maxSpd);
        f.vy = Clamp(f.vy, -7.8f, 7.8f);

        f.x += f.vx * dt;
        f.y += f.vy * dt;

        // Tail-wag animation frequency scales realistically with actual speed + thrust!
        float speedNorm = std::fabs(f.vx) / std::max(1.0f, f.baseSpeed);
        f.phase += dt * (1.8f + speedNorm * 3.1f + f.burstAmount * 3.4f);

        // Smooth visual facing flip only when velocity cleanly crosses threshold
        if (f.vx > 0.48f) f.facing = 1;
        else if (f.vx < -0.48f) f.facing = -1;

        // Smooth offscreen wrap
        if (f.x < -16.0f) {
            f.x = static_cast<float>(cols_ + 12);
            f.desiredFacing = -1;
            f.facing = -1;
            f.swimTargetY = RandFloat(6.0f, static_cast<float>(std::max(10, vb - 11)));
        } else if (f.x > cols_ + 16.0f) {
            f.x = -12.0f;
            f.desiredFacing = 1;
            f.facing = 1;
            f.swimTargetY = RandFloat(6.0f, static_cast<float>(std::max(10, vb - 11)));
        }

        float bottomBound = FloorHeightFAt(f.x) - 3.6f;
        if (f.species == FishSpecies::Angel || f.species == FishSpecies::ReefBig) {
            bottomBound -= 1.8f;
        }
        if (f.y < topBound) {
            f.y = topBound;
            if (f.vy < 0.0f) f.vy = -f.vy * 0.25f;
        } else if (f.y > bottomBound) {
            f.y = bottomBound;
            if (f.vy > 0.0f) f.vy = -f.vy * 0.25f;
        }

        if (RandInt(0, 760) == 0) {
            SpawnBubble(f.x + f.facing * 3.0f, f.y - 0.5f, 0);
        }
    }
}

void Aquarium::UpdateTurtle(float dt) {
    const int vb = VisibleBottom();
    turtle_.phase += dt * 1.95f;

    // Realistic flipper-stroke propulsion: strong smooth surge on downstroke, serene coast between strokes
    float stroke = std::max(0.0f, std::sin(turtle_.phase));
    float thrust = 1.85f + stroke * stroke * 2.55f;
    float targetVx = thrust * turtle_.facing + CurrentXAt(turtle_.x, turtle_.y) * 0.35f;
    float targetVy = Clamp((turtle_.targetY - turtle_.y) * 0.28f, -1.0f, 1.0f)
                   - stroke * 0.42f + 0.18f;

    if (RandInt(0, 260) == 0) {
        turtle_.targetY = RandFloat(7.0f, static_cast<float>(std::max(12, vb / 2)));
    }

    if (mouseActive_) {
        float dx = turtle_.x - mouseX_;
        float dy = (turtle_.y - mouseY_) * 1.8f;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 15.0f && dist > 0.1f) {
            targetVx += (dx / dist) * 4.2f;
            targetVy += ((turtle_.y - mouseY_) / dist) * 2.2f;
        }
    }

    turtle_.vx += (targetVx - turtle_.vx) * std::min(1.0f, dt * 1.7f);
    turtle_.vy += (targetVy - turtle_.vy) * std::min(1.0f, dt * 1.8f);
    turtle_.x += turtle_.vx * dt;
    turtle_.y += turtle_.vy * dt;

    if (turtle_.x > cols_ + 18.0f) {
        turtle_.facing = -1;
        turtle_.vx = -2.4f;
        turtle_.y = RandFloat(7.0f, static_cast<float>(std::max(11, vb / 2)));
        turtle_.targetY = turtle_.y;
    } else if (turtle_.x < -20.0f) {
        turtle_.facing = 1;
        turtle_.vx = 2.4f;
        turtle_.y = RandFloat(7.0f, static_cast<float>(std::max(11, vb / 2)));
        turtle_.targetY = turtle_.y;
    }
    turtle_.y = Clamp(turtle_.y, 6.5f, static_cast<float>(std::max(11, vb - 13)));

    if (RandInt(0, 380) == 0) {
        SpawnBubble(turtle_.x + (turtle_.facing > 0 ? 11.0f : 1.0f), turtle_.y, RandInt(0, 1));
    }
}

void Aquarium::UpdateSeahorses(float dt) {
    for (auto& sh : seahorses_) {
        sh.phase += dt * 1.65f;
        float cur = CurrentXAt(sh.x, sh.y);
        float targetX = sh.baseX + std::sin(sh.phase * 0.45f) * 2.8f + cur * 1.4f;
        float targetY = sh.baseY + std::sin(sh.phase) * 1.5f;

        if (mouseActive_) {
            float dx = sh.x - mouseX_;
            float dy = (sh.y - mouseY_) * 1.8f;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < 11.0f && dist > 0.1f) {
                targetX += (dx / dist) * 3.8f;
                targetY += ((sh.y - mouseY_) / dist) * 1.8f;
            }
        }
        sh.x += (targetX - sh.x) * std::min(1.0f, dt * 2.2f);
        sh.y += (targetY - sh.y) * std::min(1.0f, dt * 2.2f);
    }
}

void Aquarium::UpdateJellyfish(float dt) {
    const int vb = VisibleBottom();
    for (auto& j : jellies_) {
        j.phase += dt * j.pulseSpeed;
        float pulse = std::sin(j.phase);
        // Rhythmic bell contraction shoots upward, followed by serene lazy sink
        float targetVy = (pulse > 0.2f) ? -2.45f * std::pow(pulse, 0.8f) : 0.92f;
        j.vy += (targetVy - j.vy) * std::min(1.0f, dt * 2.8f);

        float cur = CurrentXAt(j.x, j.y);
        float targetVx = cur * 0.85f + std::sin(time_ * 0.32f + j.phase * 0.5f) * 0.65f;
        j.vx += (targetVx - j.vx) * std::min(1.0f, dt * 1.4f);

        if (mouseActive_) {
            float dx = j.x - mouseX_;
            float dy = (j.y - mouseY_) * 1.8f;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < 13.0f && dist > 0.1f) {
                j.vx += (dx / dist) * 3.4f * dt;
                j.vy += ((j.y - mouseY_) / dist) * 2.4f * dt;
            }
        }

        j.vx = Clamp(j.vx, -2.1f, 2.1f);
        j.x += j.vx * dt;
        j.y += j.vy * dt;

        // Smooth tentacle drag lag behind movement
        j.trailOffset += (-j.vx * 0.85f - j.trailOffset) * std::min(1.0f, dt * 3.0f);

        if (j.x < 5.0f) { j.x = 5.0f; j.vx = std::fabs(j.vx); }
        if (j.x > cols_ - 13.0f) { j.x = static_cast<float>(cols_ - 13); j.vx = -std::fabs(j.vx); }

        float maxY = static_cast<float>(std::max(12, vb - 13));
        if (j.y < 5.5f) { j.y = 5.5f; j.vy = 0.75f; }
        if (j.y > maxY) { j.y = maxY; j.vy = -1.6f; }
    }
}

void Aquarium::UpdateCrab(float dt) {
    crab_.animPhase += dt * (crab_.pauseTimer > 0.0f ? 1.2f : 5.4f);
    if (crab_.startledTimer > 0.0f) crab_.startledTimer = std::max(0.0f, crab_.startledTimer - dt);

    float crabY = FloorHeightFAt(crab_.x + 6.0f) - 2.0f;
    if (mouseActive_) {
        float dx = (crab_.x + 6.0f) - mouseX_;
        float dy = (crabY - mouseY_) * 1.8f;
        if (std::sqrt(dx * dx + dy * dy) < 12.0f) {
            if (crab_.startledTimer <= 0.1f) {
                Sparkle dust{ crab_.x + 5.0f, crabY + 1.0f, 0.35f, L'.' };
                sparkles_.push_back(dust);
            }
            crab_.startledTimer = 1.5f;
            crab_.pauseTimer = 0.0f;
            crab_.vx = (dx >= 0.0f) ? 8.2f : -8.2f;
        }
    }

    if (crab_.pauseTimer > 0.0f) {
        crab_.pauseTimer = std::max(0.0f, crab_.pauseTimer - dt);
        return;
    }

    crab_.x += crab_.vx * dt;
    if (crab_.x < 4.0f) {
        crab_.x = 4.0f;
        crab_.vx = RandFloat(2.4f, 4.0f);
        crab_.pauseTimer = RandFloat(1.2f, 3.2f);
    } else if (crab_.x > cols_ - 17.0f) {
        crab_.x = static_cast<float>(cols_ - 17);
        crab_.vx = -RandFloat(2.4f, 4.0f);
        crab_.pauseTimer = RandFloat(1.2f, 3.2f);
    } else if (crab_.startledTimer <= 0.0f && RandInt(0, 280) == 0) {
        crab_.pauseTimer = RandFloat(1.4f, 4.0f);
        if (RandInt(0, 1) == 0) crab_.vx = -crab_.vx;
        crab_.vx = Clamp(crab_.vx, -4.0f, 4.0f);
    } else if (crab_.startledTimer <= 0.0f) {
        crab_.vx = Clamp(crab_.vx, -4.0f, 4.0f);
    }
}

void Aquarium::UpdateChest(float dt) {
    int fy = FloorYAt(chest_.x + 6);
    if (mouseActive_) {
        float dx = (chest_.x + 6.0f) - mouseX_;
        float dy = (fy - 2.0f - mouseY_) * 1.8f;
        if (std::sqrt(dx * dx + dy * dy) < 9.5f && chest_.openTimer <= 0.0f) {
            chest_.openTimer = 3.2f;
            chest_.nextOpenIn = RandFloat(11.0f, 19.0f);
        }
    }

    if (chest_.openTimer > 0.0f) {
        chest_.openTimer = std::max(0.0f, chest_.openTimer - dt);
        if (RandInt(0, 6) == 0) {
            SpawnBubble(chest_.x + RandFloat(3.0f, 9.0f), static_cast<float>(fy - 3), RandInt(0, 2), true);
        }
    } else {
        chest_.nextOpenIn -= dt;
        if (chest_.nextOpenIn <= 0.0f) {
            chest_.openTimer = 3.4f;
            chest_.nextOpenIn = RandFloat(12.0f, 22.0f);
        }
    }
}

void Aquarium::UpdateBubbles(float dt) {
    if (bubbles_.size() < 28 && RandInt(0, 22) == 0) {
        int x = RandInt(3, std::max(4, cols_ - 4));
        SpawnBubble(static_cast<float>(x), FloorHeightFAt(static_cast<float>(x)) - 1.0f);
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

        float cur = CurrentXAt(it->x, it->y);
        float svx = 0.0f, svy = 0.0f;
        SampleSwirlFlow(it->x, it->y, svx, svy);
        it->y += (it->vy + svy * 0.45f) * dt;
        it->x += (cur * 0.5f + svx * 0.65f + std::sin(time_ * 3.0f + it->phase) * 1.15f) * dt;
        it->growTimer -= dt;
        if (it->growTimer <= 0.0f && it->size < 3) {
            it->size++;
            it->growTimer = RandFloat(2.5f, 4.5f);
        }

        float surfaceY = 2.6f - SurfaceWaveAt(it->x) * 0.45f;
        if (it->y <= surfaceY) {
            it->y = surfaceY;
            it->popping = true;
            it->popTimer = 0.22f;
        }
        ++it;
    }
}

void Aquarium::UpdateRipples(float dt) {
    for (auto it = ripples_.begin(); it != ripples_.end();) {
        it->radius += dt * 10.5f;
        it->life -= dt * 1.25f;
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
        it->y -= dt * 1.6f;
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

void Aquarium::PutFishLine(int x, int y, const wchar_t* str, COLORREF bodyCol, COLORREF accentCol, COLORREF eyeCol) {
    if (y < 0 || y >= rows_) return;
    for (int i = 0; str[i] != L'\0'; ++i) {
        wchar_t c = str[i];
        if (c == L' ') continue;
        COLORREF col = bodyCol;
        if (c == L'o' || c == L'\x00B0' || c == L'\x00BA' || c == L'@' || c == L'\'') col = eyeCol;
        else if (c == L'|' || c == L')' || c == L'(' || c == L'=') col = accentCol;
        PutChar(x + i, y, c, col);
    }
}

void Aquarium::PutMaskedSprite(int x, int y, const wchar_t* const* lines, const char* const* masks, int count,
                               COLORREF bodyCol, COLORREF finCol, COLORREF accentCol, COLORREF eyeCol) {
    for (int r = 0; r < count; ++r) {
        int py = y + r;
        if (py < 0 || py >= rows_) continue;
        const wchar_t* line = lines[r];
        const char* mask = masks[r];
        for (int i = 0; line[i] != L'\0'; ++i) {
            char m = (mask && mask[i] != '\0') ? mask[i] : '1';
            if (m == '0') {
                // Opaque interior space: cleanly masks background light rays/dots inside solid bodies
                int px = x + i;
                if (px >= 0 && px < cols_) grid_[static_cast<size_t>(py) * cols_ + px].ch = L' ';
                continue;
            }
            wchar_t ch = line[i];
            if (ch == L' ') continue;
            COLORREF col = bodyCol;
            if (m == '2') col = finCol;
            else if (m == '3') col = accentCol;
            else if (m == '4') col = eyeCol;
            else if (m == '5') col = LerpColor(bodyCol, eyeCol, 0.45f);
            PutChar(x + i, py, ch, col);
        }
    }
}

void Aquarium::DrawLightRays() {
    const Theme& t = CurrentTheme();
    const int vb = VisibleBottom();
    const COLORREF rayBright = LerpColor(t.lightRay, t.waterSurface, 0.28f);

    for (int y = 4; y < vb - 2; ++y) {
        COLORREF bg = BgColorAtRow(y);
        for (int x = 0; x < cols_; ++x) {
            // Subtle organic spacing so rays feel airy & silky rather than solid blocks
            if (((x + y) & 1) != 0) continue;
            float intensity = LightIntensityAt(static_cast<float>(x), static_cast<float>(y));
            if (intensity < 0.22f) continue;

            wchar_t ch = L'.';
            if (intensity > 0.72f) ch = ((x + y * 3) % 4 == 0) ? L':' : L'\'';
            else if (intensity > 0.48f) ch = L'\x00B7';
            else ch = L'.';

            float blend = Clamp((intensity - 0.18f) * 1.15f, 0.0f, 1.0f);
            COLORREF col = LerpColor(bg, (intensity > 0.62f) ? rayBright : t.lightRay, blend);
            PutChar(x, y, ch, col);
        }
    }
}

void Aquarium::DrawMarineSnow() {
    const Theme& t = CurrentTheme();
    for (const auto& ms : specks_) {
        int ix = static_cast<int>(std::round(ms.x));
        int iy = static_cast<int>(std::round(ms.y));
        if (iy < 5 || iy >= FloorYAt(ix) - 1) continue;
        float glow = 0.22f + 0.14f * std::sin(time_ * 1.4f + ms.phase);
        COLORREF col = LerpColor(BgColorAtRow(iy), t.waterSurface, glow);
        PutChar(ix, iy, ms.ch, col);
    }
}

void Aquarium::DrawSwirls() {
    const Theme& t = CurrentTheme();
    for (const auto& sw : swirls_) {
        float lifeFade = 1.0f;
        if (sw.age < 1.3f) lifeFade = sw.age / 1.3f;
        else if (sw.age > sw.maxLife - 2.0f) lifeFade = std::max(0.0f, (sw.maxLife - sw.age) / 2.0f);
        if (lifeFade <= 0.03f) continue;

        float dir = (sw.spinSpeed >= 0.0f) ? 1.0f : -1.0f;
        int cx = static_cast<int>(std::round(sw.x));
        int cy = static_cast<int>(std::round(sw.y));
        if (cy >= 5 && cy < FloorYAt(cx) - 1 && lifeFade * sw.strength > 0.45f) {
            COLORREF coreCol = LerpColor(BgColorAtRow(cy), t.waterSurface, 0.36f * lifeFade * sw.strength);
            PutChar(cx, cy, (std::sin(sw.angle * 1.5f) > 0.0f) ? L'\x00B0' : L'@', coreCol);
        }

        // Draw two graceful spiral vortex arms using orientation-matched ASCII curves
        constexpr int STEPS = 11;
        for (int arm = 0; arm < 2; ++arm) {
            float baseAng = sw.angle + arm * PI;
            for (int s = 1; s <= STEPS; ++s) {
                float u = static_cast<float>(s) / static_cast<float>(STEPS);
                float theta = baseAng + u * 3.25f * dir;
                float rx = sw.radius * u * 1.55f;
                float ry = sw.radius * u * 0.62f;

                int px = static_cast<int>(std::round(sw.x + std::cos(theta) * rx));
                int py = static_cast<int>(std::round(sw.y + std::sin(theta) * ry));
                if (py < 5 || py >= FloorYAt(px) - 1) continue;

                // Tangent direction along spiral determines curved glyph
                float tangX = -std::sin(theta) * dir * 1.55f;
                float tangY =  std::cos(theta) * dir * 0.62f;
                float ratio = std::fabs(tangY) / (std::fabs(tangX) + 0.01f);

                wchar_t ch = L'~';
                if (u > 0.82f) {
                    ch = (s % 2 == 0) ? L'\x00B7' : L'.';
                } else if (ratio < 0.32f) {
                    ch = (std::sin(theta) > 0.0f) ? L'_' : L'~';
                } else if (ratio > 1.15f) {
                    ch = (std::cos(theta) > 0.0f) ? L')' : L'(';
                } else {
                    ch = ((tangX * tangY) > 0.0f) ? L'\\' : L'/';
                }

                float alpha = (1.0f - u * 0.68f) * lifeFade * sw.strength * 0.52f;
                COLORREF col = LerpColor(BgColorAtRow(py),
                                         (u < 0.45f) ? t.waterFoam : t.waterSurface,
                                         Clamp(alpha, 0.0f, 0.72f));
                PutChar(px, py, ch, col);
            }
        }
    }
}

void Aquarium::DrawSurface() {
    const Theme& t = CurrentTheme();
    const COLORREF bgTop = BgColorAtRow(2);
    const COLORREF sub1 = LerpColor(bgTop, t.waterSurface, 0.58f);
    const COLORREF sub2 = LerpColor(bgTop, t.waterSurface, 0.28f);

    for (int x = 0; x < cols_; ++x) {
        float h  = SurfaceWaveAt(static_cast<float>(x));
        float hL = SurfaceWaveAt(static_cast<float>(x - 1));
        float hR = SurfaceWaveAt(static_cast<float>(x + 1));
        float slope = hR - hL;
        bool isPeak = (h >= hL && h >= hR && h > 0.50f);

        // Row 1: crisp curling ocean wave-crest spirals (хвилі-завихри: .-/~@~ )
        if (isPeak) {
            PutChar(x, 1, (h > 0.74f) ? L'@' : L'c', t.waterFoam);
            if (x > 0) PutChar(x - 1, 1, L'~', LerpColor(t.waterSurface, t.waterFoam, 0.80f));
            if (x > 1) PutChar(x - 2, 1, L'/', LerpColor(t.waterSurface, t.waterFoam, 0.55f));
            if (x + 1 < cols_) PutChar(x + 1, 1, L'~', LerpColor(t.waterSurface, t.waterFoam, 0.62f));
            if (x + 2 < cols_) PutChar(x + 2, 1, L'\x00B0', LerpColor(bgTop, t.waterFoam, 0.45f));
        } else if (h > 0.42f && ((x + static_cast<int>(time_ * 4.5f)) % 8 == 0)) {
            PutChar(x, 1, L'\x00B7', LerpColor(bgTop, t.waterFoam, 0.45f));
        }

        // Row 2: smooth continuous curling waterline
        wchar_t waveCh = L'~';
        if (isPeak && h > 0.62f)          waveCh = L')';
        else if (h > 0.52f)               waveCh = L'\x2248';
        else if (h > 0.20f)               waveCh = L'~';
        else if (h > -0.12f)              waveCh = (std::fabs(slope) > 0.08f) ? L'~' : L'-';
        else if (h > -0.45f)              waveCh = L'_';
        else                              waveCh = L',';

        float crestBlend = Clamp((h + 0.55f) * 0.65f, 0.0f, 1.0f);
        COLORREF waveCol = LerpColor(LerpColor(bgTop, t.waterSurface, 0.74f), t.waterFoam, crestBlend * 0.72f);
        PutChar(x, 2, waveCh, waveCol);

        // Row 3: gentle undulating subsurface curls (isolated crest curls, never long blocks of '(')
        float s3  = std::sin(x * 0.16f - time_ * 1.95f + h * 1.6f);
        float s3L = std::sin((x - 1) * 0.16f - time_ * 1.95f + hL * 1.6f);
        float s3R = std::sin((x + 1) * 0.16f - time_ * 1.95f + hR * 1.6f);
        bool s3Peak = (s3 >= s3L && s3 >= s3R && s3 > 0.72f);

        if (s3Peak) {
            PutChar(x, 3, L'@', sub1);
            if (x > 0) PutChar(x - 1, 3, L'(', sub1);
            if (x + 1 < cols_) PutChar(x + 1, 3, L'~', sub1);
        } else if (s3 > -0.10f) {
            wchar_t ch3 = (s3 > 0.55f) ? L'\x2248' : ((s3 > 0.20f) ? L'~' : L'-');
            COLORREF col3 = LerpColor(sub2, sub1, Clamp((s3 + 0.10f) * 0.85f, 0.0f, 1.0f));
            PutChar(x, 3, ch3, col3);
        } else if (h < -0.25f) {
            PutChar(x, 3, L'\x00B7', sub2);
        }

        // Row 4: delicate curling under-wave wisps fading softly into deep water
        float s4  = std::sin(x * 0.14f + time_ * 1.35f - h * 1.1f);
        float s4L = std::sin((x - 1) * 0.14f + time_ * 1.35f - hL * 1.1f);
        float s4R = std::sin((x + 1) * 0.14f + time_ * 1.35f - hR * 1.1f);
        if (s4 >= s4L && s4 >= s4R && s4 > 0.78f) {
            PutChar(x, 4, L'~', sub2);
            if (x + 1 < cols_) PutChar(x + 1, 4, L')', sub2);
        } else if (s4 > 0.52f) {
            PutChar(x, 4, (s4 > 0.74f) ? L'~' : L'\x00B7', sub2);
        }
    }
}

void Aquarium::DrawDistantSeabed() {
    const Theme& t = CurrentTheme();
    const COLORREF farCrest = LerpColor(t.bgBottom, t.sandDark, 0.44f);
    const COLORREF farFill  = LerpColor(t.bgBottom, t.sandDark, 0.25f);

    for (int x = 0; x < cols_; ++x) {
        float bfy = bgFloorF_[x];
        int by = static_cast<int>(std::floor(bfy));
        int fy = FloorYAt(x);
        if (by >= fy) continue;

        float bL = (x > 0) ? bgFloorF_[x - 1] : bfy;
        float bR = (x + 1 < cols_) ? bgFloorF_[x + 1] : bfy;
        float slope = bR - bL;

        wchar_t crestCh = L'_';
        if (slope < -0.22f) crestCh = L'/';
        else if (slope > 0.22f) crestCh = L'\\';
        else {
            float frac = bfy - std::floor(bfy);
            crestCh = (frac < 0.35f) ? L'-' : ((frac < 0.70f) ? L'_' : L'.');
        }
        PutChar(x, by, crestCh, farCrest);

        for (int y = by + 1; y < fy; ++y) {
            if (((x * 3 + y * 5) % 7) == 0) {
                PutChar(x, y, L'.', farFill);
            }
        }
    }
}

void Aquarium::DrawSeabed() {
    const Theme& t = CurrentTheme();
    const COLORREF sandLit = LerpColor(t.sand, t.waterFoam, 0.28f);
    const COLORREF sandMid = LerpColor(t.sand, t.sandDark, 0.48f);
    const COLORREF sandDeep = LerpColor(t.sandDark, t.bgBottom, 0.35f);

    // Smooth continuous sand dunes with sub-cell slope characters & dancing sun caustics!
    for (int x = 0; x < cols_; ++x) {
        float fyF = floorHeightF_[x];
        int fy = static_cast<int>(std::floor(fyF));
        float frac = fyF - static_cast<float>(fy);
        float fL = (x > 0) ? floorHeightF_[x - 1] : fyF;
        float fR = (x + 1 < cols_) ? floorHeightF_[x + 1] : fyF;
        float slope = fR - fL;

        float caustic = LightIntensityAt(static_cast<float>(x), fyF);
        COLORREF crestCol = LerpColor(t.sand, sandLit, caustic * 0.85f);

        wchar_t topCh = L'_';
        if (slope < -0.24f)      topCh = L'/';
        else if (slope > 0.24f)  topCh = L'\\';
        else if (frac < 0.28f)   topCh = ((x % 6) == 0) ? L'~' : L'-';
        else if (frac < 0.66f)   topCh = ((x % 5) == 0) ? L'~' : L'_';
        else                     topCh = ((x % 3) == 0) ? L',' : L'.';

        // Clear cells below sand crest so distant ridge or kelp roots don't clutter sand texture
        for (int y = fy; y < rows_; ++y) {
            if (y >= 0) grid_[static_cast<size_t>(y) * cols_ + x].ch = L' ';
        }
        PutChar(x, fy, topCh, crestCol);

        // Layered detailed sand sediment & gentle wind-ripple bands
        for (int y = fy + 1; y < rows_; ++y) {
            int dy = y - fy;
            wchar_t ch = sandNoise_[static_cast<size_t>(x) * 10 + (dy % 10)];
            if (dy == 1 && ((x + static_cast<int>(fyF * 2.0f)) % 11) < 2) {
                ch = L'~';
            } else if (dy == 2 && ((x + 4) % 13) == 0) {
                ch = L'_';
            }
            COLORREF col = (dy == 1) ? LerpColor(t.sand, sandLit, caustic * 0.45f)
                         : (dy == 2) ? sandMid
                         : (dy <= 4) ? t.sandDark
                                     : sandDeep;
            PutChar(x, y, ch, col);
        }
    }

    // Shells, starfish & smooth seabed pebbles
    for (const auto& sd : shells_) {
        int fy = FloorYAt(sd.x);
        COLORREF col = (sd.kind == 1) ? t.coral1
                     : (sd.kind == 0) ? LerpColor(t.sand, t.waterFoam, 0.35f)
                                      : t.rockLight;
        PutChar(sd.x, fy + sd.dy, sd.ch, col);
    }

    // Detailed multi-tone organic boulders with opaque interior, sunlit top ridge & optional moss
    auto clearSpan = [&](int x0, int x1, int y) {
        if (y < 0 || y >= rows_) return;
        for (int px = std::max(0, x0); px <= std::min(cols_ - 1, x1); ++px) {
            grid_[static_cast<size_t>(y) * cols_ + px].ch = L' ';
        }
    };
    for (const auto& r : rocks_) {
        int midX = r.x + r.width / 2;
        int fy = FloorYAt(midX);
        float caustic = LightIntensityAt(static_cast<float>(midX), static_cast<float>(fy - 2));
        COLORREF topHi = LerpColor(t.rockLight, t.waterFoam, caustic * 0.25f);
        COLORREF rockDark = LerpColor(t.rock, t.bgBottom, 0.30f);

        if (r.height >= 4 && r.width >= 11) {
            clearSpan(r.x + 3, r.x + r.width - 4, fy - 3);
            for (int dx = 4; dx < r.width - 4; ++dx)
                PutChar(r.x + dx, fy - 4, L'_', topHi);
            PutChar(r.x + 3, fy - 3, L'/', topHi);
            PutChar(r.x + r.width - 4, fy - 3, L'\\', t.rock);
            for (int dx = 4; dx < r.width - 4; ++dx) {
                wchar_t ch = (r.mossy && (dx % 3 == 0)) ? L',' : ((dx == 4) ? L'.' : L'\'');
                PutChar(r.x + dx, fy - 3, ch, (r.mossy && (dx % 3 == 0)) ? t.seaweedDark : topHi);
            }
        }
        if (r.height >= 3 && r.width >= 9) {
            clearSpan(r.x + 2, r.x + r.width - 3, fy - 2);
            if (r.height == 3) {
                for (int dx = 3; dx < r.width - 3; ++dx)
                    PutChar(r.x + dx, fy - 3, L'_', topHi);
            }
            PutChar(r.x + 2, fy - 2, L'/', topHi);
            PutChar(r.x + r.width - 3, fy - 2, L'\\', t.rock);
            for (int dx = 3; dx < r.width - 3; ++dx) {
                bool isMoss = r.mossy && (dx % 4 == 1);
                wchar_t ch = isMoss ? L'~' : ((dx % 3 == 0) ? L'`' : L'.');
                PutChar(r.x + dx, fy - 2, ch, isMoss ? t.seaweedDark : t.rockLight);
            }
        }
        clearSpan(r.x + 1, r.x + r.width - 2, fy - 1);
        PutChar(r.x + 1, fy - 1, L'/', t.rockLight);
        PutChar(r.x + r.width - 2, fy - 1, L'\\', rockDark);
        for (int dx = 2; dx < r.width - 2; ++dx) {
            PutChar(r.x + dx, fy - 1, (dx % 2 == 0) ? L':' : L'.',
                    (dx < r.width / 2) ? t.rock : rockDark);
        }
        clearSpan(r.x, r.x + r.width - 1, fy);
        PutChar(r.x, fy, L'(', t.rock);
        PutChar(r.x + r.width - 1, fy, L')', rockDark);
        for (int dx = 1; dx < r.width - 1; ++dx) {
            PutChar(r.x + dx, fy, (dx % 3 == 0) ? L',' : L'_', rockDark);
        }
    }

    // Detailed corals + gently swaying animated sea anemones
    for (const auto& c : corals_) {
        int fy = FloorYAt(c.x);
        COLORREF col = (c.colorSlot == 0) ? t.coral1 : ((c.colorSlot == 1) ? t.coral2 : t.coral3);
        COLORREF tipCol = LerpColor(col, t.waterFoam, 0.38f);
        COLORREF baseCol = LerpColor(col, t.bgBottom, 0.25f);

        if (c.style == 0) {
            // Detailed branching staghorn coral with bright tips
            PutChar(c.x - 2, fy - 3, L'*', tipCol);
            PutChar(c.x + 2, fy - 3, L'*', tipCol);
            PutChar(c.x,     fy - 3, L'o', tipCol);
            PutString(c.x - 2, fy - 2, L"\\ | /", col);
            PutString(c.x - 1, fy - 1, L"\\|/", col);
            PutString(c.x,     fy,     L"Y", baseCol);
        } else if (c.style == 1) {
            // Delicate filigree sea fan coral
            PutString(c.x - 2, fy - 3, L".o*o.", tipCol);
            PutString(c.x - 3, fy - 2, L"o\\ | /o", col);
            PutString(c.x - 2, fy - 1, L"\\ \\|/ /", col);
            PutString(c.x - 1, fy,     L"\\|/", baseCol);
        } else if (c.style == 2) {
            // Animated swaying Sea Anemone (home for clownfish!)
            float sway = std::sin(time_ * 2.1f + c.phase + CurrentXAt(static_cast<float>(c.x), static_cast<float>(fy)));
            int shift = (sway > 0.35f) ? 1 : ((sway < -0.35f) ? -1 : 0);
            const wchar_t* tips = (shift > 0) ? L"~* ~* ~" : ((shift < 0) ? L"~ *~ *~" : L"* ~ * ~");
            const wchar_t* tent = (shift > 0) ? L"/ ) / )" : ((shift < 0) ? L"\\ ( \\ (" : L"\\ | | /");
            PutString(c.x - 3 + shift, fy - 2, tips, tipCol);
            PutString(c.x - 3,         fy - 1, tent, col);
            PutString(c.x - 2,         fy,     L"(===)", baseCol);
        } else {
            // Clustered tube sponges / reef chimneys
            PutString(c.x - 2, fy - 3, L" o  ", tipCol);
            PutString(c.x - 2, fy - 2, L"(|)o", col);
            PutString(c.x - 2, fy - 1, L"|||)", col);
            PutString(c.x - 2, fy,     L"(__)", baseCol);
        }
    }
}

void Aquarium::DrawSeagrass() {
    const Theme& t = CurrentTheme();
    for (const auto& g : seagrasses_) {
        int baseY = FloorYAt(g.x);
        float cur = CurrentXAt(static_cast<float>(g.x), static_cast<float>(baseY));
        COLORREF col = g.bright ? t.seaweedLight : t.seaweedDark;
        for (int seg = 0; seg < g.height; ++seg) {
            float sway = std::sin(time_ * 1.9f + g.phase - seg * 0.55f) * (0.35f + seg * 0.35f) + cur * 0.4f;
            int dx = static_cast<int>(std::round(sway));
            wchar_t ch = (dx > 0) ? L'/' : ((dx < 0) ? L'\\' : ((seg == g.height - 1) ? L'\'' : L'|'));
            PutChar(g.x + dx, baseY - seg, ch, col);
        }
    }
}

void Aquarium::DrawTreasureChest() {
    const Theme& t = CurrentTheme();
    int cx = chest_.x;
    int fy = FloorYAt(cx + 6);
    bool open = (chest_.openTimer > 0.0f);
    COLORREF trim = LerpColor(t.chestWood, t.chestGold, 0.35f);

    // Clear chest interior so distant ridge / sand noise never bleeds through
    for (int y = fy - (open ? 3 : 2); y <= fy; ++y) {
        if (y < 0 || y >= rows_) continue;
        for (int x = std::max(0, cx); x <= std::min(cols_ - 1, cx + 12); ++x) {
            grid_[static_cast<size_t>(y) * cols_ + x].ch = L' ';
        }
    }

    if (open) {
        bool shimmer = (static_cast<int>(time_ * 6.0f) % 2 == 0);
        PutString(cx + 1, fy - 4, L".---------.", t.chestWood);
        PutString(cx,     fy - 3, shimmer ? L"/ * o * o *\\" : L"/ o * o * o\\", t.chestGold);
        PutString(cx,     fy - 2, L"|===[===]===|", trim);
        PutString(cx,     fy - 1, L"|  | [o] |  |", t.chestWood);
        PutString(cx,     fy,     L"|__|_____|__|", t.chestWood);
    } else {
        PutString(cx + 1, fy - 3, L".---------.", t.chestWood);
        PutString(cx,     fy - 2, L"/===========\\", trim);
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
        COLORREF leafCol = s.bright ? t.seaweedDark : LerpColor(t.seaweedDark, t.seaweedLight, 0.55f);
        float cur = CurrentXAt(static_cast<float>(s.baseX), static_cast<float>(baseY - s.height / 2));

        for (int seg = 0; seg < s.height; ++seg) {
            float ratio = static_cast<float>(seg) / static_cast<float>(std::max(1, s.height));
            // Traveling wave propagating smoothly from root to tip + ambient current
            float waveArg = time_ * s.speed + s.phase - seg * 0.38f;
            float sway = std::sin(waveArg) * (0.35f + ratio * 1.75f) + cur * ratio * 1.1f;
            float bendOffset = s.bend * ratio * ratio;
            int x = s.baseX + static_cast<int>(std::round(sway + bendOffset));
            int y = baseY - seg;

            if (seg == s.height - 1) {
                PutChar(x, y, L'~', LerpColor(mainCol, t.waterFoam, 0.12f));
                if (s.leafy) PutChar(x + ((sway >= 0.0f) ? 1 : -1), y, L'`', leafCol);
                continue;
            }

            float slope = std::cos(waveArg) + (s.bend + cur) * 0.22f;
            wchar_t stem = (slope > 0.38f) ? L')' : ((slope < -0.38f) ? L'(' : ((seg % 2 == 0) ? L'S' : L'|'));
            PutChar(x, y, stem, mainCol);

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
        COLORREF bodyCol = t.fishColors[sh.colorIndex % 6];
        COLORREF finCol = LerpColor(bodyCol, t.waterFoam, 0.35f);
        bool flutter = (static_cast<int>(sh.phase * 3.0f) % 2 == 0);

        if (sh.facing >= 0) {
            const wchar_t* spr[4] = { L" ,o>", flutter ? L"(( )" : L"(())", L" \\|", L" (@)" };
            const char*    msk[4] = { " 145", "2112", " 11", " 212" };
            PutMaskedSprite(ix, iy, spr, msk, 4, bodyCol, finCol, bodyCol, t.fishEye);
        } else {
            const wchar_t* spr[4] = { L"<o, ", flutter ? L"( ))" : L"(())", L" |/ ", L"(@) " };
            const char*    msk[4] = { "541 ", "2112", " 11 ", "212 " };
            PutMaskedSprite(ix, iy, spr, msk, 4, bodyCol, finCol, bodyCol, t.fishEye);
        }
    }
}

void Aquarium::DrawTurtle() {
    const Theme& t = CurrentTheme();
    int ix = static_cast<int>(std::round(turtle_.x));
    int iy = static_cast<int>(std::round(turtle_.y));
    float s = std::sin(turtle_.phase);
    int strokeStage = (s > 0.35f) ? 0 : ((s < -0.35f) ? 2 : 1);

    COLORREF shellCol = t.turtleShell;
    COLORREF patternCol = LerpColor(t.turtleShell, t.sand, 0.32f);
    COLORREF bodyCol = t.turtleBody;

    // Sleeker, prettier, slightly smaller 4-row x 13-col sea turtle
    if (turtle_.facing >= 0) {
        const wchar_t* flippers = (strokeStage == 0) ? L" \\__/  \\_/"
                                : (strokeStage == 1) ? L"  /_\\  /_\\"
                                                     : L" /_/  \\_\\ ";
        const wchar_t* spr[4] = {
            L"  .-----.   ",
            L" / .---. \\_.",
            L"=| (===) |o>",
            flippers
        };
        const char* msk[4] = {
            "  1111111   ",
            " 10333330122",
            "210333330142",
            " 2222  222  "
        };
        PutMaskedSprite(ix, iy - 1, spr, msk, 4, shellCol, bodyCol, patternCol, t.fishEye);
    } else {
        const wchar_t* flippers = (strokeStage == 0) ? L" \\_/  \\__/"
                                : (strokeStage == 1) ? L" /_\\  /_\\ "
                                                     : L" /_/  \\_\\ ";
        const wchar_t* spr[4] = {
            L"   .-----.  ",
            L"._/ .---. \\ ",
            L"<o| (===) |=",
            flippers
        };
        const char* msk[4] = {
            "   1111111  ",
            "22103333301 ",
            "241033333012",
            " 222  2222  "
        };
        PutMaskedSprite(ix, iy - 1, spr, msk, 4, shellCol, bodyCol, patternCol, t.fishEye);
    }
}

void Aquarium::DrawShark() {
    if (!shark_.active) return;
    const Theme& t = CurrentTheme();
    int ix = static_cast<int>(std::round(shark_.x));
    int iy = static_cast<int>(std::round(shark_.y));
    bool tailFlex = (std::sin(shark_.phase) > 0.0f);

    // Theme-harmonized steel-blue dorsal back, pale silver underbelly, dark gills, gleaming eye & white teeth
    COLORREF dorsalCol = LerpColor(t.rockLight, t.waterSurface, 0.24f);
    COLORREF bellyCol  = LerpColor(t.rockLight, t.fishEye, 0.56f);
    COLORREF gillCol   = LerpColor(t.rock, t.bgBottom, 0.20f);
    COLORREF eyeCol    = t.fishEye;

    if (shark_.facing >= 0) {
        const wchar_t* sprA[9] = {
            L"                         __                    ",
            L"                        ( `\\                   ",
            L" ,                       )  `\\                 ",
            L";' `.                    (    `\\__             ",
            L" ;   `.         __..---''         `~~~-._      ",
            L"  `.   `.____..--''                  (o  `--._ ",
            L"    >                _.-'    .((      ._     ) ",
            L"  .`.-`--...__    .-'   -.___....-(|/|/|/|/'   ",
            L" ;.'          `..--`.___.'`''''-----...--'     "
        };
        const wchar_t* sprB[9] = {
            L"                         __                    ",
            L"                        ( `\\                   ",
            L"  ,                      )  `\\                 ",
            L" ;' `.                   (    `\\__             ",
            L"  ;   `.        __..---''         `~~~-._      ",
            L"   `.   `.___..--''                  (o  `--._ ",
            L"     >               _.-'    .((      ._     ) ",
            L"   .`.-`--..__    .-'   -.___....-(|/|/|/|/'   ",
            L"  ;.'         `..--`.___.'`''''-----...--'     "
        };
        const char* mskA[9] = {
            "                         11                    ",
            "                        1011                   ",
            " 1                       10011                 ",
            "11011                    100001111             ",
            " 100011         1111111110000000001111111      ",
            "  11000111111111111000000000000000000340011111 ",
            "    100000000000000002222000033300000022000001 ",
            "  111111111111000022200022222222222555555555   ",
            " 111          2222222222222222222222222222     "
        };
        const char* mskB[9] = {
            "                         11                    ",
            "                        1011                   ",
            "  1                      10011                 ",
            " 11011                   100001111             ",
            "  100011        1111111110000000001111111      ",
            "   1100011111111111000000000000000000340011111 ",
            "     10000000000000002222000033300000022000001 ",
            "   11111111111000022200022222222222555555555   ",
            "  111         2222222222222222222222222222     "
        };
        PutMaskedSprite(ix, iy - 4, tailFlex ? sprA : sprB, tailFlex ? mskA : mskB, 9,
                        dorsalCol, bellyCol, gillCol, eyeCol);
    } else {
        const wchar_t* sprA[9] = {
            L"                    __                         ",
            L"                   /' )                        ",
            L"                 /'  (                       , ",
            L"             __/'    )                    .' `;",
            L"      _.-~~~'         ``---..__         .'   ; ",
            L" _.--'  o)                  ``--..____.'   .'  ",
            L" (     _.      )).    `-._                <    ",
            L"   `\\|\\|\\|\\|)-....___.-   `-.    __...--'-`.   ",
            L"     `--...-----''''`'.___.`--..`          `.; "
        };
        const wchar_t* sprB[9] = {
            L"                    __                         ",
            L"                   /' )                        ",
            L"                 /'  (                      ,  ",
            L"             __/'    )                   .' `; ",
            L"      _.-~~~'         ``---..__        .'   ;  ",
            L" _.--'  o)                  ``--..___.'   .'   ",
            L" (     _.      )).    `-._               <     ",
            L"   `\\|\\|\\|\\|)-....___.-   `-.    __..--'-`.    ",
            L"     `--...-----''''`'.___.`--..`         `.;  "
        };
        const char* mskA[9] = {
            "                    11                         ",
            "                   1101                        ",
            "                 11001                       1 ",
            "             111100001                    11011",
            "      1111111000000000111111111         110001 ",
            " 11111004300000000000000000011111111111100011  ",
            " 100000220000003330000222200000000000000001    ",
            "   555555555222222222220002220000111111111111  ",
            "     2222222222222222222222222222          111 "
        };
        const char* mskB[9] = {
            "                    11                         ",
            "                   1101                        ",
            "                 11001                      1  ",
            "             111100001                   11011 ",
            "      1111111000000000111111111        110001  ",
            " 1111100430000000000000000001111111111100011   ",
            " 10000022000000333000022220000000000000001     ",
            "   55555555522222222222000222000011111111111   ",
            "     2222222222222222222222222222         111  "
        };
        PutMaskedSprite(ix, iy - 4, tailFlex ? sprA : sprB, tailFlex ? mskA : mskB, 9,
                        dorsalCol, bellyCol, gillCol, eyeCol);
    }
}

void Aquarium::DrawSubmarine(bool surfaceOverlay) {
    if (sub_.state == SubmarineState::Inactive) return;

    const Theme& t = CurrentTheme();
    int ix = static_cast<int>(std::round(sub_.x));
    int iy = std::max(2, static_cast<int>(std::round(sub_.y)));

    COLORREF hullCol  = LerpColor(t.chestGold, t.sand, 0.28f);
    COLORREF towerCol = LerpColor(t.rockLight, t.chestGold, 0.30f);
    COLORREF trimCol  = t.waterFoam;
    COLORREF glowCol  = t.fishEye;

    bool surfaced = (sub_.y <= 4.2f);
    // When surfaced, render the full vessel cleanly after DrawSurface() so waves never overwrite its deck/portholes
    if (surfaceOverlay != surfaced) return;

    // Animated spinning propeller blade
    int pFrame = static_cast<int>(sub_.propPhase) % 3;
    wchar_t propCh = (pFrame == 0) ? L'+' : ((pFrame == 1) ? L'x' : L'*');

    if (sub_.facing >= 0) {
        wchar_t row2[24] = L"+=|  (o) (o) (o)   )  ";
        row2[0] = propCh;
        const wchar_t* spr[4] = {
            L"        .===.         ",
            L"  .____|_____|____.   ",
            row2,
            L"  `---------------'   "
        };
        const char* msk[4] = {
            "        22222         ",
            "  11111222222211111   ",
            "33100343034303430001  ",
            "  11111111111111111   "
        };
        PutMaskedSprite(ix, iy, spr, msk, 4, hullCol, towerCol, trimCol, glowCol);
    } else {
        wchar_t row2[24] = L"  (   (o) (o) (o)  |=+";
        row2[21] = propCh;
        const wchar_t* spr[4] = {
            L"         .===.        ",
            L"   .____|_____|____.  ",
            row2,
            L"   `---------------'  "
        };
        const char* msk[4] = {
            "         22222        ",
            "   11111222222211111  ",
            "  10003430343034300133",
            "   11111111111111111  "
        };
        PutMaskedSprite(ix, iy, spr, msk, 4, hullCol, towerCol, trimCol, glowCol);
    }

    int towerCenter = ix + (sub_.facing >= 0 ? 10 : 11);

    if (surfaced) {
        // Gentle white foam lapping cleanly against the bow and stern at waterline
        PutChar(ix + 1,  iy + 1, L'~', t.waterFoam);
        PutChar(ix + 20, iy + 1, L'~', t.waterFoam);
    }

    // Extending & rotating periscope above conning tower!
    if (sub_.periscopeExt > 0.15f) {
        PutChar(towerCenter, iy - 1, L'|', towerCol);
    }
    if (sub_.periscopeExt > 0.65f) {
        bool glint = (sub_.state == SubmarineState::LookingAround) &&
                     (static_cast<int>(time_ * 4.5f) % 5 == 0);
        wchar_t lensCh = glint ? L'*' : L'o';
        COLORREF lensCol = glint ? t.food : glowCol;

        if (sub_.lookDir > 0) {
            PutChar(towerCenter,     iy - 2, L',', towerCol);
            PutChar(towerCenter + 1, iy - 2, L'=', towerCol);
            PutChar(towerCenter + 2, iy - 2, lensCh, lensCol);
        } else if (sub_.lookDir < 0) {
            PutChar(towerCenter - 2, iy - 2, lensCh, lensCol);
            PutChar(towerCenter - 1, iy - 2, L'=', towerCol);
            PutChar(towerCenter,     iy - 2, L',', towerCol);
        } else {
            PutChar(towerCenter - 1, iy - 2, L'(', towerCol);
            PutChar(towerCenter,     iy - 2, lensCh, lensCol);
            PutChar(towerCenter + 1, iy - 2, L')', towerCol);
        }
    }
}

void Aquarium::DrawJellyfish() {
    const Theme& t = CurrentTheme();
    for (const auto& j : jellies_) {
        int ix = static_cast<int>(std::round(j.x));
        int iy = static_cast<int>(std::round(j.y));
        bool contracted = std::sin(j.phase) > 0.15f;

        if (contracted) {
            const wchar_t* spr[3] = { L"  .---.  ", L" / o o \\ ", L" (_____) " };
            const char*    msk[3] = { "  22222  ", " 1040401 ", " 2222222 " };
            PutMaskedSprite(ix, iy, spr, msk, 3, t.jellyfish, t.jellyfishGlow, t.jellyfish, t.jellyfishGlow);
        } else {
            const wchar_t* spr[3] = { L" _.---._ ", L"/  o o  \\", L"(_______)" };
            const char*    msk[3] = { " 2222222 ", "100404001", "222222222" };
            PutMaskedSprite(ix, iy, spr, msk, 3, t.jellyfish, t.jellyfishGlow, t.jellyfish, t.jellyfishGlow);
        }

        // Procedurally waving, velocity-trailing delicate tentacles fading into the water
        const int tentacleX[4] = { 1, 3, 5, 7 };
        for (int seg = 0; seg < 3; ++seg) {
            float fade = 1.0f - seg * 0.28f;
            COLORREF tCol = LerpColor(BgColorAtRow(iy + 3 + seg), t.jellyfish, fade);
            for (int k = 0; k < 4; ++k) {
                if (contracted && (k == 0 || k == 3) && seg == 2) continue;
                float wave = std::sin(time_ * 3.4f + j.phase - seg * 0.95f + k * 0.8f);
                float drag = j.trailOffset * (0.45f + seg * 0.55f);
                int tx = ix + tentacleX[k] + static_cast<int>(std::round(wave * 0.55f + drag));
                wchar_t ch = (seg == 2) ? ((wave > 0.0f) ? L'\'' : L':')
                           : ((wave > 0.35f) ? L')' : ((wave < -0.35f) ? L'(' : L'|'));
                PutChar(tx, iy + 3 + seg, ch, tCol);
            }
        }
    }
}

void Aquarium::DrawSingleFish(const Fish& f) {
    const Theme& t = CurrentTheme();
    int ix = static_cast<int>(std::round(f.x));
    int iy = static_cast<int>(std::round(f.y));

    COLORREF baseBody = t.fishColors[f.colorIndex % 6];
    COLORREF baseAccent = t.fishColors[(f.colorIndex + 1) % 6];
    COLORREF baseFin = LerpColor(baseBody, baseAccent, 0.52f);
    COLORREF eyeCol = t.fishEye;

    // Depth parallax shading: distant fish softly blend into ocean depth
    COLORREF bg = BgColorAtRow(iy);
    COLORREF bodyCol = baseBody;
    COLORREF accentCol = baseAccent;
    COLORREF finCol = baseFin;
    if (f.depthLayer == 0) {
        bodyCol   = LerpColor(bg, baseBody, 0.68f);
        accentCol = LerpColor(bg, baseAccent, 0.68f);
        finCol    = LerpColor(bg, baseFin, 0.68f);
        eyeCol    = LerpColor(bg, t.fishEye, 0.78f);
    } else if (f.depthLayer == 2) {
        bodyCol = ScaleColor(baseBody, 1.06f);
    }

    bool right = (f.facing >= 0);
    bool wag = (static_cast<int>(f.phase) % 2 == 0);

    switch (f.species) {
        case FishSpecies::Minnow: {
            if (right) {
                const wchar_t* s = (f.variant == 0) ? (wag ? L"><>" : L">=>")
                                 : (f.variant == 1) ? (wag ? L"><\x00B0>" : L">=\x00B0>")
                                                    : (wag ? L"><(\x00B0>" : L">=(\x00B0>");
                PutFishLine(ix - 2, iy, s, bodyCol, finCol, eyeCol);
            } else {
                const wchar_t* s = (f.variant == 0) ? (wag ? L"<><" : L"<=<")
                                 : (f.variant == 1) ? (wag ? L"<\x00B0><" : L"<\x00B0=<")
                                                    : (wag ? L"<\x00B0)><" : L"<\x00B0)=<");
                PutFishLine(ix - 2, iy, s, bodyCol, finCol, eyeCol);
            }
            break;
        }

        case FishSpecies::Tropical: {
            if (f.variant == 0) {
                // Iconic crisp 1-line tropical fish
                if (right) {
                    PutFishLine(ix - 3, iy, wag ? L"><((\x00B0>" : L">=((\x00B0>", bodyCol, accentCol, eyeCol);
                } else {
                    PutFishLine(ix - 3, iy, wag ? L"<\x00B0))><" : L"<\x00B0))=<", bodyCol, accentCol, eyeCol);
                }
            } else if (f.variant == 1) {
                // Classic Asciiquarium 3-row mini dart fish (6 cols)
                if (right) {
                    const wchar_t* spr[3] = { L"  ,\\ ", wag ? L">=(\x00B0>" : L"><(\x00B0>", L"  '/ " };
                    const char*    msk[3] = { "  12 ", "22345", "  12 " };
                    PutMaskedSprite(ix - 2, iy - 1, spr, msk, 3, bodyCol, finCol, accentCol, eyeCol);
                } else {
                    const wchar_t* spr[3] = { L" /,  ", wag ? L"<\x00B0)=<" : L"<\x00B0)><", L" \\`  " };
                    const char*    msk[3] = { " 21  ", "54322", " 21  " };
                    PutMaskedSprite(ix - 2, iy - 1, spr, msk, 3, bodyCol, finCol, accentCol, eyeCol);
                }
            } else {
                // Classic Asciiquarium 3-row round reef fish (6 cols)
                if (right) {
                    const wchar_t* spr[3] = { L"  __ ", wag ? L"\\/ o\\" : L">< o\\", L"/\\__/" };
                    const char*    msk[3] = { "  22 ", "21 41", "21111" };
                    PutMaskedSprite(ix - 2, iy - 1, spr, msk, 3, bodyCol, finCol, accentCol, eyeCol);
                } else {
                    const wchar_t* spr[3] = { L" __  ", wag ? L"/o \\/" : L"/o ><", L"\\__/\\" };
                    const char*    msk[3] = { " 22  ", "14 12", "11112" };
                    PutMaskedSprite(ix - 2, iy - 1, spr, msk, 3, bodyCol, finCol, accentCol, eyeCol);
                }
            }
            break;
        }

        case FishSpecies::Clownfish: {
            // Compact striped 3-row x 9-col Clownfish with bright white bands!
            COLORREF stripeCol = eyeCol;
            if (right) {
                const wchar_t* spr[3] = {
                    L"   .---. ",
                    wag ? L"\\ /| |o \\" : L"> /| |o \\",
                    wag ? L"/ \\|_|__/" : L"> \\|_|__/"
                };
                const char* msk[3] = {
                    "   21111 ",
                    "2 1303401",
                    "2 1313111"
                };
                PutMaskedSprite(ix - 4, iy - 1, spr, msk, 3, bodyCol, finCol, stripeCol, eyeCol);
            } else {
                const wchar_t* spr[3] = {
                    L" .---.   ",
                    wag ? L"/ o| |\\ /" : L"/ o| |\\ <",
                    wag ? L"\\__|_|/ \\" : L"\\__|_|/ <"
                };
                const char* msk[3] = {
                    " 11112   ",
                    "1043031 2",
                    "1113131 2"
                };
                PutMaskedSprite(ix - 4, iy - 1, spr, msk, 3, bodyCol, finCol, stripeCol, eyeCol);
            }
            break;
        }

        case FishSpecies::Puffer: {
            if (f.scaredTimer > 0.0f) {
                // Cute puffed-up spiny ball!
                bool pulse = (static_cast<int>(time_ * 9.0f) % 2 == 0);
                const wchar_t* spr[3] = {
                    pulse ? L" \\.-./ " : L" *.-.* ",
                    L"-(o_o)-",
                    pulse ? L" /'-'\\ " : L" *'-'* "
                };
                const char* msk[3] = {
                    " 21112 ",
                    "2141412",
                    " 21112 "
                };
                PutMaskedSprite(ix - 3, iy - 1, spr, msk, 3, bodyCol, accentCol, finCol, eyeCol);
            } else if (right) {
                const wchar_t* spr[3] = { L"  .--. ", wag ? L"><( o> " : L">=( o> ", L"  `--' " };
                const char*    msk[3] = { "  1111 ", "223 45 ", "  1111 " };
                PutMaskedSprite(ix - 3, iy - 1, spr, msk, 3, bodyCol, finCol, accentCol, eyeCol);
            } else {
                const wchar_t* spr[3] = { L" .--.  ", wag ? L" <o )><" : L" <o )=<", L" `--'  " };
                const char*    msk[3] = { " 1111  ", " 54 322", " 1111  " };
                PutMaskedSprite(ix - 3, iy - 1, spr, msk, 3, bodyCol, finCol, accentCol, eyeCol);
            }
            break;
        }

        case FishSpecies::Angel: {
            // Classic graceful Asciiquarium Angelfish (compact 5 rows x 9 cols)
            if (right) {
                const wchar_t* spr[5] = {
                    L"    }\\   ",
                    wag ? L"\\ .'  `\\ " : L"/ .'  `\\ ",
                    wag ? L"}}<  (o> " : L"}>{  (o> ",
                    wag ? L"/ `,  .' " : L"\\ `,  .' ",
                    L"    }/   "
                };
                const char* msk[5] = {
                    "    22   ",
                    "2 110011 ",
                    "22100345 ",
                    "2 110011 ",
                    "    22   "
                };
                PutMaskedSprite(ix - 4, iy - 2, spr, msk, 5, bodyCol, finCol, accentCol, eyeCol);
            } else {
                const wchar_t* spr[5] = {
                    L"   /{    ",
                    wag ? L" /'  `. /" : L" /'  `. \\",
                    wag ? L" <o)  >{{" : L" <o)  }<{",
                    wag ? L" `.  ,' \\" : L" `.  ,' /",
                    L"   \\{    "
                };
                const char* msk[5] = {
                    "   22    ",
                    " 110011 2",
                    " 54300122",
                    " 110011 2",
                    "   22    "
                };
                PutMaskedSprite(ix - 4, iy - 2, spr, msk, 5, bodyCol, finCol, accentCol, eyeCol);
            }
            break;
        }

        case FishSpecies::ReefBig: {
            if (f.variant == 0) {
                // Classic Asciiquarium Curvy Reef Fish / Royal Koi (graceful 5 rows x 13 cols)
                if (right) {
                    const wchar_t* spr[5] = {
                        L"      ,--,   ",
                        wag ? L"_   _\\.--'-. " : L"    _\\.--'-. ",
                        wag ? L"\\'.\"   // o\\ " : L">'-'   // o\\ ",
                        wag ? L"/'.-_  \\\\  / " : L">'._-  \\\\  / ",
                        L"     `\"-(/\"  "
                    };
                    const char* msk[5] = {
                        "      2222   ",
                        "2   12111111 ",
                        "221100033041 ",
                        "221110033001 ",
                        "     111221  "
                    };
                    PutMaskedSprite(ix - 6, iy - 2, spr, msk, 5, bodyCol, finCol, accentCol, eyeCol);
                } else {
                    const wchar_t* spr[5] = {
                        L"   ,--,      ",
                        wag ? L" .-'--./_   _" : L" .-'--./_    ",
                        wag ? L" /o \\\\   \".'/" : L" /o \\\\   '-'<",
                        wag ? L" \\  //  _-.'\\" : L" \\  //  -_.'<",
                        L"  \"\\)-\"`     "
                    };
                    const char* msk[5] = {
                        "   2222      ",
                        " 11111121   2",
                        " 140330001122",
                        " 100330011122",
                        "  122111     "
                    };
                    PutMaskedSprite(ix - 6, iy - 2, spr, msk, 5, bodyCol, finCol, accentCol, eyeCol);
                }
            } else {
                // Classic Asciiquarium Sleek Torpedo Reef Fish (compact 5 rows x 12 cols)
                if (right) {
                    const wchar_t* spr[5] = {
                        L"     \\:.    ",
                        wag ? L"\\;, ,;\\\\,,  " : L">;, ,;\\\\,,  ",
                        L"  \\\\;;::::o ",
                        L"  //;;::::< ",
                        wag ? L" /;` `///`  " : L" >;` `///`  "
                    };
                    const char* msk[5] = {
                        "     222    ",
                        "222 112211  ",
                        "  223311114 ",
                        "  223311115 ",
                        " 222 12221  "
                    };
                    PutMaskedSprite(ix - 6, iy - 2, spr, msk, 5, bodyCol, finCol, accentCol, eyeCol);
                } else {
                    const wchar_t* spr[5] = {
                        L"    .:/     ",
                        wag ? L"  ,,//;, ,;/" : L"  ,,//;, ,;<",
                        L" o::::;;//  ",
                        L" >::::;;\\\\  ",
                        wag ? L"  `\\\\\\` `;\\ " : L"  `\\\\\\` `;< "
                    };
                    const char* msk[5] = {
                        "    222     ",
                        "  112211 222",
                        " 411113322  ",
                        " 511113322  ",
                        "  12221 222 "
                    };
                    PutMaskedSprite(ix - 6, iy - 2, spr, msk, 5, bodyCol, finCol, accentCol, eyeCol);
                }
            }
            break;
        }
    }
}

void Aquarium::DrawFishLayer(int layer) {
    for (const auto& f : fishes_) {
        if (f.depthLayer == layer) {
            DrawSingleFish(f);
        }
    }
}

void Aquarium::DrawCrab() {
    const Theme& t = CurrentTheme();
    int ix = static_cast<int>(std::round(crab_.x));
    int iy = FloorYAt(ix + 6) - 1;
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
        wchar_t ch = b.popping ? ((b.popTimer > 0.11f) ? L'*' : L'\x00B7') : chars[b.size % 4];
        COLORREF col = b.golden ? t.chestGold : t.bubble;
        if (b.popping) col = LerpColor(BgColorAtRow(iy), col, b.popTimer / 0.22f);
        PutChar(ix, iy, ch, col);
    }
}

void Aquarium::DrawRipples() {
    const Theme& t = CurrentTheme();
    for (const auto& rp : ripples_) {
        int rx = static_cast<int>(std::round(rp.radius));
        int ry = std::max(1, rx / 2);
        int cx = static_cast<int>(std::round(rp.x));
        int cy = static_cast<int>(std::round(rp.y));
        COLORREF col = LerpColor(BgColorAtRow(cy), t.ripple, Clamp(rp.life, 0.0f, 1.0f));

        PutChar(cx - rx, cy, L'(', col);
        PutChar(cx + rx, cy, L')', col);
        if (rx >= 2) {
            PutChar(cx, cy - ry, L'~', col);
            PutChar(cx, cy + ry, L'~', col);
        }
        if (rx >= 4) {
            int dx = (rx * 7) / 10;
            int dy = std::max(1, (ry * 7) / 10);
            PutChar(cx - dx, cy - dy, L'.', col);
            PutChar(cx + dx, cy - dy, L'.', col);
            PutChar(cx - dx, cy + dy, L'\'', col);
            PutChar(cx + dx, cy + dy, L'\'', col);
        }
    }
}

void Aquarium::DrawSparkles() {
    const Theme& t = CurrentTheme();
    for (const auto& sp : sparkles_) {
        int iy = static_cast<int>(std::round(sp.y));
        COLORREF col = LerpColor(BgColorAtRow(iy), t.food, Clamp(sp.life * 2.2f, 0.0f, 1.0f));
        PutChar(static_cast<int>(std::round(sp.x)), iy, sp.ch, col);
    }
}

void Aquarium::Render() {
    if (cols_ <= 0 || rows_ <= 0) return;
    ClearGrid();
    DrawLightRays();
    DrawMarineSnow();
    DrawSwirls();
    DrawDistantSeabed();
    DrawFishLayer(0);   // far depth-parallax fish behind kelp
    DrawSeaweed(false); // background kelp
    DrawSeabed();
    DrawSeagrass();
    DrawTreasureChest();
    DrawSeahorses();
    DrawTurtle();
    DrawShark();        // rare majestic unhurried Great Shark
    DrawSubmarine(false);
    DrawJellyfish();
    DrawFishLayer(1);   // midground fish
    DrawFishLayer(2);   // foreground fish
    DrawSeaweed(true);  // foreground kelp
    DrawCrab();
    DrawFood();
    DrawBubbles();
    DrawRipples();
    DrawSparkles();
    DrawSurface();
    DrawSubmarine(true); // conning tower & periscope rising above the waves!
}
