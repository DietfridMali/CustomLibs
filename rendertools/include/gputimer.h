#pragma once

// GLEW/GLAD vorher initialisieren

// =================================================================================================

struct GpuTimer {
    static constexpr int FramesInFlight = 3; // ring, um Stalls zu vermeiden
    struct Slot { GLuint q = 0; const char* label = nullptr; };
    std::vector<Slot> slots[FramesInFlight];
    int frame = 0;

    void begin(const char* label) {
        if (slots[frame].empty() || slots[frame].back().q != 0) {
            slots[frame].push_back({});
        }
        Slot s;
        glGenQueries(1, &s.q);
        s.label = label;
        glBeginQuery(GL_TIME_ELAPSED, s.q);
        slots[frame].push_back(s);
    }

    void end() {
        glEndQuery(GL_TIME_ELAPSED);
        // markiere, dass der letzte begin/end abgeschlossen ist
        // (kein extra code nötig; wir lesen Ergebnisse erst später)
    }

    // Call einmal pro Frame NACH dem Swap → liest Frame-(frame+1)%N aus
    void resolve() {
        int readIdx = (frame + 1) % FramesInFlight;
        for (auto& s : slots[readIdx]) {
            if (!s.q) continue;
            GLuint64 ns = 0;
            glGetQueryObjectui64v(s.q, GL_QUERY_RESULT, &ns); // blockt jetzt i. d. R. nicht
            printf("[GPU] %-24s : %.3f ms\n", s.label ? s.label : "(unnamed)", ns / 1e6);
            glDeleteQueries(1, &s.q);
        }
        slots[readIdx].clear();
        frame = (frame + 1) % FramesInFlight;
    }
};

inline GpuTimer gputimer;

// Komfort-RAII
struct ScopedGpuTimer {
    ScopedGpuTimer(const char* label) { gputimer.begin(label); }
    ~ScopedGpuTimer() { gputimer.end(); }
};

// =================================================================================================
