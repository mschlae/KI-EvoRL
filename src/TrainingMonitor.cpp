// ============================================================
//  TrainingMonitor.cpp
// ============================================================
#include "TrainingMonitor.h"

#include <sstream>

namespace {
    constexpr size_t MAX_HISTORY = 2000; // gedeckelt, damit das JSON nicht explodiert
}

TrainingMonitor::TrainingMonitor()
    : start_(std::chrono::steady_clock::now()) {
}

void TrainingMonitor::update(const GenRecord& rec,
    float globalBestFitness,
    int   globalBestFood,
    float curriculum,
    bool  injecting,
    int   totalGenerations) {
    std::lock_guard<std::mutex> lk(m_);
    latest_ = rec;
    globalBestFitness_ = globalBestFitness;
    globalBestFood_ = globalBestFood;
    curriculum_ = curriculum;
    injecting_ = injecting;
    totalGenerations_ = totalGenerations;

    history_.push_back(rec);
    if (history_.size() > MAX_HISTORY) {
        // Aeltesten Block verwerfen (Block-Erase ist guenstiger als einzelnes erase()).
        history_.erase(history_.begin(), history_.begin() + (history_.size() - MAX_HISTORY));
    }
}

void TrainingMonitor::setFinished() {
    finished_.store(true);
    running_.store(false);
}

void TrainingMonitor::setReplay(std::string json) {
    std::lock_guard<std::mutex> lk(m_);
    replay_ = std::move(json);
}

std::string TrainingMonitor::replayJson() const {
    std::lock_guard<std::mutex> lk(m_);
    if (replay_.empty()) return "{\"frames\":[]}";
    return replay_;
}

std::string TrainingMonitor::toJson() const {
    std::lock_guard<std::mutex> lk(m_);

    double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start_).count();

    std::ostringstream os;
    os.setf(std::ios::fixed);
    os.precision(3);

    os << '{';
    os << "\"running\":" << (running_.load() ? "true" : "false") << ',';
    os << "\"finished\":" << (finished_.load() ? "true" : "false") << ',';
    os << "\"stopRequested\":" << (stopRequested_.load() ? "true" : "false") << ',';
    os << "\"generation\":" << latest_.gen << ',';
    os << "\"totalGenerations\":" << totalGenerations_ << ',';
    os << "\"elapsedSeconds\":" << elapsed << ',';
    os << "\"bestFitness\":" << latest_.best << ',';
    os << "\"avgFitness\":" << latest_.avg << ',';
    os << "\"avgFood\":" << latest_.avgFood << ',';
    os << "\"bestFood\":" << latest_.bestFood << ',';
    os << "\"bestSpf\":" << latest_.bestSpf << ',';
    os << "\"avgSpf\":" << latest_.avgSpf << ',';
    os << "\"mutation\":" << latest_.mutation << ',';
    os << "\"diversity\":" << latest_.diversity << ',';
    os << "\"species\":" << latest_.species << ',';
    os << "\"globalBestFitness\":" << globalBestFitness_ << ',';
    os << "\"globalBestFood\":" << globalBestFood_ << ',';
    os << "\"curriculum\":" << curriculum_ << ',';
    os << "\"injecting\":" << (injecting_ ? "true" : "false") << ',';

    // Verlaufsdaten als parallele Arrays (kompakt, schnell im Browser zu parsen).
    auto writeArr = [&](const char* name, auto getter, bool last = false) {
        os << '"' << name << "\":[";
        for (size_t i = 0; i < history_.size(); ++i) {
            if (i) os << ',';
            os << getter(history_[i]);
        }
        os << ']';
        if (!last) os << ',';
        };

    os << "\"history\":{";
    writeArr("gen", [](const GenRecord& r) { return r.gen; });
    writeArr("best", [](const GenRecord& r) { return r.best; });
    writeArr("avg", [](const GenRecord& r) { return r.avg; });
    writeArr("avgFood", [](const GenRecord& r) { return r.avgFood; });
    writeArr("bestFood", [](const GenRecord& r) { return r.bestFood; });
    writeArr("bestSpf", [](const GenRecord& r) { return r.bestSpf; });
    writeArr("avgSpf", [](const GenRecord& r) { return r.avgSpf; });
    writeArr("diversity", [](const GenRecord& r) { return r.diversity; });
    writeArr("mutation", [](const GenRecord& r) { return r.mutation; }, /*last=*/true);
    os << '}';

    os << '}';
    return os.str();
}