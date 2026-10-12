// ============================================================
//  Replay.cpp
// ============================================================
#include "Replay.h"

#include <charconv>

namespace {
    inline void appendInt(std::string& s, int v) {
        char buf[16];
        auto r = std::to_chars(buf, buf + sizeof(buf), v);
        s.append(buf, r.ptr);
    }
}

std::string buildReplayJson(World& world, Brain& brain, int genLabel, int maxSteps, float temperature) {
    brain.resetHidden();

    std::string walls, food0;
    for (int i = 0; i < World::CELLS; i++) {
        std::string* dst = nullptr;
        if (world.grid[i] == WALL || world.grid[i] == OBSTACLE) dst = &walls;
        else if (world.grid[i] == FOOD)                         dst = &food0;
        if (dst) { if (!dst->empty()) *dst += ','; appendInt(*dst, i); }
    }

    std::vector<FoodEvent> events;
    world.foodEventSink = &events;

    std::string frames;
    frames.reserve(1024);
    auto pushFrame = [&](bool first) {
        if (!first) frames += ',';
        frames += '['; appendInt(frames, world.agentX); frames += ','; appendInt(frames, world.agentY); frames += ']';
    };
    pushFrame(true);

    while (world.alive && world.steps < maxSteps) {
        auto sens = world.getSensors();
        uint8_t mask = world.cfg.maskBlocked ? world.blockedMask() : 0;
        world.executeAction(brain.selectAction(sens, world.rng, temperature, mask));
        pushFrame(false);
    }
    world.foodEventSink = nullptr;

    std::string evJson;
    evJson.reserve(events.size() * 12);
    for (size_t i = 0; i < events.size(); i++) {
        if (i) evJson += ',';
        evJson += '['; appendInt(evJson, events[i].step);
        evJson += events[i].appeared ? ",1," : ",0,";
        appendInt(evJson, events[i].idx); evJson += ']';
    }

    std::string out;
    out.reserve(frames.size() + evJson.size() + walls.size() + food0.size() + 160);
    out += "{\"generation\":"; appendInt(out, genLabel);
    out += ",\"w\":"; appendInt(out, GRID_W);
    out += ",\"h\":"; appendInt(out, GRID_H);
    out += ",\"food\":"; appendInt(out, world.foodEaten);
    out += ",\"steps\":"; appendInt(out, world.steps);
    out += ",\"walls\":["; out += walls;
    out += "],\"food0\":["; out += food0;
    out += "],\"frames\":["; out += frames;
    out += "],\"events\":["; out += evJson;
    out += "]}";
    return out;
}
