#pragma once

// EL SONIDO DEL JUEGO: los efectos de Assets/Audio (.wav) y las dos musicas
// (.ogg), y quien decide cuando suena cada uno.
//
// Hay dos clases de evento, y cada una suena desde un sitio distinto:
//
//   jugadas   colocar, rotar, quitar, deshacer, los botones del HUD, el menu.
//             Suenan DONDE pasan (FigureBuilder, GameHud, MainMenu) con
//             gameaudio::Play("Game/piece_rotate"). Ahi se sabe lo que no se
//             puede deducir despues: si la jugada entro o se rechazo, y por que.
//   estado    la luz que sale, cruza figuras y termina; el paso entre niveles;
//             menu o partida. Eso lo MIRA GameAudio::Tick una vez por frame,
//             comparando con el frame anterior. Si sonara desde donde pasa, la
//             luz tendria que saber de audio (y la luz es un dibujo que reparte
//             SurfaceLight sobre un recorrido que calcula LightTrace, sin
//             motor): el recorrido ya dice todo lo que hace falta oir.
//
// Los efectos se leen como .wav PROPIOS porque el motor todavia no carga un
// AudioClip desde archivo (docs/API-motor.md, seccion 12): la musica si, por
// streaming. Se leen por FileSystem::ReadAll, asi que en un Build salen del
// paquete igual que los niveles. Todos se cargan al empezar (son ~16 MB de
// PCM): cargar el primero recien en el primer clic es un tiron justo cuando
// la persona esta mirando.
//
// Un estatico, como PuzzleSession y GameFlow: todos los scripts viven en el
// mismo Scripts.dll.

#include "../Flow/GameFlow.h"
#include "../Flow/MenuKit.h"
#include "../Puzzle/PuzzleSession.h"

#include "Engine/Audio/AudioService.h"
#include "Engine/Diagnostics/Log.h"
#include "Engine/IO/FileSystem.h"
#include "Engine/Scene/SceneService.h"
#include "Engine/Text.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <climits>
#include <cwchar>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace gravitational {

namespace gameaudio {

// Todos los efectos, relativos a Assets/Audio y sin extension.
inline const char* const kAllClips[] = {
    "Ui/ui_tick", "Ui/ui_move", "Ui/ui_accept", "Ui/ui_back", "Ui/ui_toggle", "Ui/ui_denied", "Ui/menu_wake",
    "Ui/menu_line", "Ui/menu_open", "Ui/menu_fold", "Ui/menu_close", "Ui/quit_prompt",
    "Game/piece_pick", "Game/piece_place_1", "Game/piece_place_2", "Game/piece_place_3", "Game/piece_place_invalid",
    "Game/piece_rotate", "Game/piece_remove", "Game/undo", "Game/restart", "Game/pause", "Game/resume", "Game/step",
    "Game/hud_notice",
    "Light/light_emit", "Light/light_hum_loop", "Light/light_enter_1", "Light/light_enter_2", "Light/light_enter_3",
    "Light/light_enter_4", "Light/light_enter_5", "Light/light_edge", "Light/light_lost", "Light/light_blocked",
    "Light/light_orbit_loop", "Light/blackhole_fall", "Light/whitehole_return", "Light/wormhole", "Light/solved",
    "Flow/level_collapse", "Flow/level_card", "Flow/level_grow", "Flow/game_complete"};

// Un .wav PCM de 16 bits. Se recorren los trozos del RIFF en vez de suponer
// una cabecera de 44 bytes: un editor de audio que vuelva a guardar el archivo
// (FL Studio, Audacity) puede meter un LIST o un fact antes del data, y entonces
// el sonido seria su propia cabecera leida como PCM.
struct WavPcm {
    std::vector<int16_t> pcm; // intercalado
    int channels = 0;
    int rate = 0;
    int64_t Frames() const { return channels > 0 ? static_cast<int64_t>(pcm.size()) / channels : 0; }
};

inline bool ParseWav(const std::string& bytes, WavPcm& out, std::wstring& error) {
    auto u16 = [&](size_t at) { return static_cast<uint32_t>(static_cast<uint8_t>(bytes[at]) | static_cast<uint8_t>(bytes[at + 1]) << 8); };
    auto u32 = [&](size_t at) { return u16(at) | u16(at + 2) << 16; };
    if (bytes.size() < 12 || bytes.compare(0, 4, "RIFF") != 0 || bytes.compare(8, 4, "WAVE") != 0) {
        error = L"no es un WAV";
        return false;
    }
    int channels = 0, rate = 0, bits = 0, format = 0;
    size_t at = 12;
    while (at + 8 <= bytes.size()) {
        const std::string id = bytes.substr(at, 4);
        const size_t size = u32(at + 4);
        const size_t body = at + 8;
        if (body + size > bytes.size()) break;
        if (id == "fmt " && size >= 16) {
            format = static_cast<int>(u16(body));
            channels = static_cast<int>(u16(body + 2));
            rate = static_cast<int>(u32(body + 4));
            bits = static_cast<int>(u16(body + 14));
        } else if (id == "data") {
            // 1 = PCM; 0xFFFE = extensible, que con 16 bits tambien es PCM entero.
            if ((format != 1 && format != 0xFFFE) || bits != 16 || channels < 1 || channels > 2) {
                error = L"solo PCM de 16 bits, mono o estereo";
                return false;
            }
            out.channels = channels;
            out.rate = rate;
            out.pcm.resize(size / 2);
            std::memcpy(out.pcm.data(), bytes.data() + body, out.pcm.size() * 2);
            return true;
        }
        at = body + size + (size & 1); // los trozos van alineados a 2
    }
    error = L"sin trozo de datos";
    return false;
}

inline bool ReadWav(const std::wstring& path, WavPcm& out, std::wstring& error) {
    std::string bytes;
    if (!strace::FileSystem::ReadAll(path, bytes)) {
        error = L"no se pudo leer";
        return false;
    }
    return ParseWav(bytes, out, error);
}

inline strace::AudioClipPtr DecodeWav(const std::string& bytes, std::wstring& error) {
    WavPcm wav;
    if (!ParseWav(bytes, wav, error)) return nullptr;
    return strace::MakeClipFromPcm16(wav.pcm.data(), wav.pcm.size(), wav.channels, wav.rate);
}

inline std::unordered_map<std::string, strace::AudioClipPtr>& Clips() {
    static std::unordered_map<std::string, strace::AudioClipPtr> clips;
    return clips;
}

inline strace::AudioClipPtr Load(const std::string& name) {
    auto& clips = Clips();
    auto it = clips.find(name);
    if (it != clips.end()) return it->second;
    const std::wstring path = strace::SceneService::ProjectRoot() + L"/Assets/Audio/" + strace::Utf8ToWide(name) + L".wav";
    std::string bytes;
    std::wstring error;
    strace::AudioClipPtr clip;
    if (!strace::FileSystem::ReadAll(path, bytes)) error = L"no se pudo leer";
    else clip = DecodeWav(bytes, error);
    if (!clip) strace::Log::Warn(L"GameAudio: '" + path + L"': " + error);
    clips[name] = clip; // tambien el que fallo: un aviso por archivo, no uno por clic
    return clip;
}

inline void LoadAll() {
    int ok = 0, total = 0;
    for (const char* name : kAllClips) {
        ++total;
        ok += Load(name) ? 1 : 0;
    }
    strace::Log::Info(L"GameAudio: " + std::to_wstring(ok) + L" de " + std::to_wstring(total) + L" efectos cargados");
}

// GRAV_AUDIO_LOG=1 (en el entorno): cada sonido que se dispara, al log. Una
// corrida automatica no tiene quien escuche; asi se ve QUE sono y CUANDO, que
// es lo que se puede comprobar sin oido.
inline bool LogPlays() {
    static const bool on = kit::AutoPick("GRAV_AUDIO_LOG") == 1;
    return on;
}

// Un efecto. Uno que no se pudo cargar no suena (y ya lo dijo el log).
inline strace::VoiceId Play(const std::string& name, float db = 0.0f, bool loop = false) {
    strace::AudioClipPtr clip = Load(name);
    if (LogPlays()) strace::Log::Info(L"GameAudio: suena " + strace::Utf8ToWide(name));
    return clip ? strace::AudioService::PlaySfx(clip, db, loop) : strace::kInvalidVoice;
}

// Un efecto que suena dentro de `delay` segundos (lo lleva GameAudio::Tick).
// Es para lo que va al compas de una animacion: las cuatro laminas del menu
// aterrizan al FINAL del plegado, no cuando empieza.
struct Pending {
    std::string name;
    float delay = 0.0f;
    float db = 0.0f;
};
inline std::vector<Pending>& Queue() {
    static std::vector<Pending> queue;
    return queue;
}
inline void PlayLater(const std::string& name, float delay, float db = 0.0f) { Queue().push_back({name, delay, db}); }

// El boton PASO pone la pausa Y avanza: sin esto sonarian "pausa" y "paso"
// juntos por una sola tecla.
inline bool& SuppressPauseSound() {
    static bool suppress = false;
    return suppress;
}

// Colocar alterna tres tomas: la misma toma veinte veces seguidas se oye como
// una maquina, que es justo lo que una pieza de cristal no tiene que sonar.
inline void PlayPlace() {
    static int next = 0;
    Play("Game/piece_place_" + std::to_string(next + 1));
    next = (next + 1) % 3;
}

} // namespace gameaudio

// LA MUSICA DE LA PARTIDA, en partes (la compuso la persona en FL Studio,
// Assets/Audio/Music/Fragments/): Start y Part1..PartN, todas en mi menor a
// 144 BPM. Cada parte son 8 compases (13,33 s) y el inicio 12 (20 s): medido
// en el audio el 22/10/2026 - el pulso a 144 da 0,85 de autocorrelacion y
// ningun otro tempo pasa de 0,12. Una parte puede durar un multiplo de la mas
// corta ("el doble de lo mismo, para que las matematicas encajen").
//
// QUE SUENA EN CADA NIVEL (PlanForLevel), lo que pidio la persona:
//
//   nivel 1..N    su propia parte, en bucle (hay 17 partes para 23 niveles)
//   N+1..final-1  DOS partes fijas (no al azar), intercaladas cada 2 compases:
//                 A, B, A, B... Pares de partes que se parecen en sonido y en
//                 volumen (medido: distancia espectral 1,5-2,3 dB, menos de 1 dB
//                 de diferencia de RMS), para que el cambio sea de capas y no un
//                 salto de volumen cada 3 s. Van de menos a mas energia hacia el
//                 final. Ninguna parte se repite entre pares
//   el ultimo     TODAS las partes, enteras, una detras de otra en orden al azar
//                 (bolsa: no se repite una hasta que sonaron todas, y nunca dos
//                 veces seguidas)
//
// Al pasar de nivel la musica NO vuelve a empezar: sigue en el compas en que
// iba. La cuenta es una LINEA DE TIEMPO - cuanto lleva sonando desde que
// termino el inicio -, y cada parte se lee en (linea de tiempo - su origen)
// modulo (su largo). Asi dos partes intercaladas estan siempre en el mismo
// compas.
//
// EL CAMBIO DE NIVEL ESPERA AL COMPAS (SetLevel). Primero cambiaba en la
// muestra misma: como casi todas las partes se parecen, sonaba continuo...
// salvo entre partes de melodias distintas, donde la frase se cortaba a la
// mitad. Ahora se mira en que mitad del bloque de 8 compases va la musica
// (pidio la persona, 23/10/2026):
//
//   compases 1-4   la parte vieja termina su mitad, y los compases 5-8 ya son
//                  los de la parte NUEVA (sus 4 finales)
//   compases 5-8   la parte vieja termina el bloque entero, y la nueva entra
//                  desde su compas 1
//
// En los dos casos el cambio cae en una frontera de 4 compases, que es donde
// estas partes cierran frase. El plan nuevo queda PENDIENTE (m_next) hasta su
// muestra; la ventana ya armada lo lleva adentro, asi que nada mas tiene que
// mirar el reloj. El origen del plan nuevo se fija para que su compas 1 caiga
// en el comienzo de un bloque: hoy las partes son todas de 8 compases y da lo
// mismo, pero una de 16 sin origen podria entrar por su segunda mitad.
//
// COMO SUENA SIN QUE EL MEZCLADOR SEPA PROGRAMAR NADA. El mezclador no deja
// decir "empeza esta voz en la muestra X": solo "empeza ya". Pero los cambios
// del intercalado y de la secuencia tienen que caer justo en el compas. Asi que
// no se toca una parte en bucle: se ARMA por adelantado una ventana de dos
// partes (~27 s) de la linea de tiempo segun el plan - con los cambios de parte
// adentro, cada uno con un fundido cruzado de 256 muestras -, y se toca esa
// ventana. Antes de que se acabe (a la mitad), se arma la siguiente desde la
// muestra en que va la musica y se cambia por la vieja: es el mismo sonido en
// la misma muestra, asi que el relevo no se oye. Un cambio de nivel es lo
// mismo con el plan nuevo.
//
// EL RELEVO de una voz por otra. El mezclador sabe en que muestra va cada voz
// (VoiceFrame), asi que la nueva se arma empezando justo ahi. Pero una voz
// arranca a volumen pleno: empezar a mitad de la onda es un chasquido. Y al
// parar la vieja, el motor la baja en una rampa de 256 muestras. Asi que junto
// con la nueva suena un "compensador" de 256 muestras: la nueva al reves,
// multiplicada por la rampa que le falta. Nueva + compensador = la nueva
// SUBIENDO en la misma rampa en que la vieja BAJA: un fundido cruzado de 6 ms
// calzado a la muestra. Las tres llamadas (nueva, compensador, parar la vieja)
// van seguidas, para que caigan en el mismo bloque del mezclador.
//
// Todo se probo contra una simulacion del mezclador con los archivos reales:
// fuera de los fundidos, la salida es la linea de tiempo ideal muestra por
// muestra.
class GameMusic {
public:
    static constexpr int kRamp = 256;           // AudioMixer::kRampFrames: la rampa con que el motor para una voz
    static constexpr int kRate = 44100;
    static constexpr int kMaxParts = 64;
    static constexpr int kBarsPerUnit = 8;      // compases en la parte mas corta (144 BPM, 13,33 s)
    static constexpr int kInterleaveBars = 2;   // cada cuantos compases se alterna un par

    // Los pares de los niveles sin parte propia, en numeros de parte (1 = Part1).
    // El primero es el del primer nivel sin parte (hoy el 18).
    static constexpr int kPairs[][2] = {{3, 9}, {4, 11}, {8, 10}, {2, 5}, {1, 13}};

    struct Plan {
        enum class Kind { Single, Interleave, Shuffle } kind = Kind::Single;
        int a = 0, b = 0; // partes, desde 0
        bool operator==(const Plan& o) const { return kind == o.kind && a == o.a && b == o.b; }
    };

    int Parts() const { return static_cast<int>(m_parts.size()); }

    Plan PlanForLevel(int level, int levelCount) const {
        Plan plan;
        const int n = Parts();
        if (n == 0) return plan;
        level = std::max(0, level);
        if (levelCount > 1 && level >= levelCount - 1 && n > 1) {
            plan.kind = Plan::Kind::Shuffle; // el ultimo nivel, siempre
            return plan;
        }
        if (level < n) {
            plan.a = level;
            return plan;
        }
        const int pairs = static_cast<int>(sizeof(kPairs) / sizeof(kPairs[0]));
        const int* pair = kPairs[(level - n) % pairs];
        if (pair[0] >= 1 && pair[0] <= n && pair[1] >= 1 && pair[1] <= n) {
            plan.kind = Plan::Kind::Interleave;
            plan.a = pair[0] - 1;
            plan.b = pair[1] - 1;
        } else {
            plan.a = level % n; // un par que nombra una parte que no esta: su parte en ciclo
        }
        return plan;
    }

    bool Load() {
        m_loaded = false;
        m_parts.clear();
        const std::wstring dir = strace::SceneService::ProjectRoot() + L"/Assets/Audio/Music/Fragments/";
        std::wstring error;
        if (!gameaudio::ReadWav(dir + L"Start.wav", m_intro, error)) {
            strace::Log::Warn(L"GameMusic: " + dir + L"Start.wav: " + error + L". La partida va sin musica.");
            return false;
        }
        if (!Usable(m_intro, L"Start.wav")) return false;
        // Part1, Part2... seguidas, hasta la primera que falte.
        for (int i = 1; i <= kMaxParts; ++i) {
            const std::wstring name = L"Part" + std::to_wstring(i) + L".wav";
            if (!strace::FileSystem::Exists(dir + name)) break;
            gameaudio::WavPcm w;
            if (!gameaudio::ReadWav(dir + name, w, error)) {
                strace::Log::Warn(L"GameMusic: " + dir + name + L": " + error + L". Se usan las partes hasta la anterior.");
                break;
            }
            if (!Usable(w, name)) break;
            m_parts.push_back(std::move(w));
        }
        WarnAboutStrays(dir);
        if (m_parts.empty()) {
            strace::Log::Warn(L"GameMusic: no hay Part1.wav en " + dir + L". La partida va sin musica.");
            return false;
        }
        // El largo base es la parte mas corta; las demas, multiplos exactos.
        m_unit = m_parts[0].Frames();
        for (const auto& p : m_parts) m_unit = std::min(m_unit, p.Frames());
        if (m_unit < kRamp * 64) {
            strace::Log::Warn(L"GameMusic: las partes son demasiado cortas. La partida va sin musica.");
            return false;
        }
        m_lengths.clear();
        for (int i = 0; i < Parts(); ++i) {
            const int64_t frames = m_parts[static_cast<size_t>(i)].Frames();
            const int64_t len = frames / m_unit * m_unit;
            if (len != frames)
                strace::Log::Warn(L"GameMusic: Part" + std::to_wstring(i + 1) + L".wav dura " + Seconds(frames) +
                                  L" s, que no es multiplo de " + Seconds(m_unit) + L" s (la parte mas corta): se usan sus primeros " +
                                  Seconds(len) + L" s para que el compas encaje.");
            m_lengths.push_back(len);
        }
        m_maxLen = *std::max_element(m_lengths.begin(), m_lengths.end());
        m_loaded = true;
        strace::Log::Info(L"GameMusic: inicio de " + Seconds(m_intro.Frames()) + L" s y " + std::to_wstring(Parts()) +
                          L" partes de " + Seconds(m_unit) + L" s (o multiplos)");
        return true;
    }

    // Desde el principio: el inicio y despues lo que pida el nivel.
    void Begin(int level, int levelCount, float gainDb) {
        Stop();
        if (!m_loaded) return;
        m_gain = strace::DecibelsToGain(gainDb);
        m_plan = PlanForLevel(level, levelCount);
        m_origin = 0;
        m_hasNext = false;
        ResetShuffle(0);
        Build(0);
    }

    // Otro nivel: el plan nuevo, en la proxima frontera de 4 compases (ver
    // arriba). Se compara contra lo que VA a sonar: pedir otra vez el nivel
    // pendiente no lo corre de sitio, y volver al que suena lo cancela.
    void SetLevel(int level, int levelCount) {
        if (!m_loaded || m_voice == strace::kInvalidVoice) return;
        const Plan plan = PlanForLevel(level, levelCount);
        if (plan == (m_hasNext ? m_next : m_plan)) return;
        int64_t t = Position();
        if (t < 0) t = m_windowStart + m_windowFrames;
        Commit(t);
        if (plan == m_plan) {
            m_hasNext = false; // se volvio al que suena antes del cambio: no hay cambio
        } else if (t < IntroFrames()) {
            // Durante el inicio no suena ninguna parte todavia: el plan se
            // cambia ya y su compas 1 entra cuando termine el inicio.
            m_plan = plan;
            m_origin = 0;
            m_hasNext = false;
            ResetShuffle(0);
        } else {
            const int64_t u = t - IntroFrames();
            const int64_t half = m_unit / 2;
            const int64_t phase = (u - m_origin) % m_unit;
            const int64_t blockStart = u - phase;
            m_next = plan;
            m_hasNext = true;
            if (phase < half) {
                m_nextAt = blockStart + half; // los 4 finales de la nueva
                m_nextOrigin = blockStart;
            } else {
                m_nextAt = blockStart + m_unit; // la nueva entera, desde su compas 1
                m_nextOrigin = m_nextAt;
            }
            if (plan.kind == Plan::Kind::Shuffle) ResetShuffle(m_nextAt - m_nextOrigin);
            if (gameaudio::LogPlays())
                strace::Log::Info(L"GameMusic: nivel nuevo en el compas " + std::to_wstring(phase * kBarsPerUnit / m_unit + 1) +
                                  L" del bloque; cambia en el segundo " + Seconds(IntroFrames() + m_nextAt) +
                                  (phase < half ? L" (4 finales de la nueva)" : L" (la nueva desde el principio)"));
        }
        Build(t);
    }

    // Una vez por frame: pasada la mitad de la ventana, la siguiente.
    void Tick() {
        if (!m_loaded || m_voice == strace::kInvalidVoice) return;
        const int64_t t = Position();
        if (t < 0) {
            Build(m_windowStart + m_windowFrames); // la voz se acabo (el host paro todo, o un tiron enorme): se retoma
            return;
        }
        Commit(t);
        if (t - m_windowStart >= m_windowFrames / 2) Build(t);
    }

    void Stop() {
        StopVoice(m_voice);
        StopVoice(m_comp);
    }

private:
    struct Source {
        int part = -1;   // -1 = el inicio
        int64_t pos = 0; // muestra dentro de esa parte (o del inicio)
    };
    struct Segment {     // del modo al azar: una parte entera desde `start` (linea de tiempo, tras el inicio)
        int64_t start = 0;
        int part = 0;
    };

    static void StopVoice(strace::VoiceId& v) {
        if (v != strace::kInvalidVoice) strace::AudioService::Stop(v);
        v = strace::kInvalidVoice;
    }

    static std::wstring Seconds(int64_t frames) {
        wchar_t buf[32];
        swprintf(buf, 32, L"%.2f", frames / static_cast<double>(kRate));
        return buf;
    }

    // El mezclador no remuestrea clips (AudioMixer.cpp): uno a otra frecuencia
    // sonaria en otro tono, y el relevo no calzaria nunca.
    bool Usable(const gameaudio::WavPcm& w, const std::wstring& name) const {
        const bool ok = w.rate == kRate && (&w == &m_intro || w.channels == m_intro.channels);
        if (!ok)
            strace::Log::Warn(L"GameMusic: " + name + L" tiene que ser de 44100 Hz y con los mismos canales que Start.wav.");
        return ok;
    }

    // Una Part18.wav sin Part17.wav no entra (se leen seguidas): decirlo, porque
    // en silencio se ve igual que "no exporte la 18".
    void WarnAboutStrays(const std::wstring& dir) const {
        for (const std::wstring& f : strace::FileSystem::ListFiles(dir)) {
            const size_t slash = f.find_last_of(L"/\\");
            const std::wstring name = slash == std::wstring::npos ? f : f.substr(slash + 1);
            if (name.size() < 9 || name.compare(0, 4, L"Part") != 0) continue;
            const long n = std::wcstol(name.c_str() + 4, nullptr, 10); // wcstol y no _wtoi: el Build Web no es MSVC
            if (n > Parts())
                strace::Log::Warn(L"GameMusic: " + name + L" no se usa: las partes se leen seguidas desde Part1 y la serie se corta en Part" +
                                  std::to_wstring(Parts() + 1) + L".wav (falta o no se pudo leer).");
        }
    }

    int64_t IntroFrames() const { return m_intro.Frames(); }

    // En que muestra de la linea de tiempo va la musica (contando el inicio), o
    // -1 si la voz ya no suena.
    int64_t Position() const {
        const int64_t f = strace::AudioService::Mixer().VoiceFrame(m_voice);
        return f < 0 ? -1 : m_windowStart + f;
    }

    // El plan pendiente pasa a ser el que suena cuando la musica llega a su
    // muestra. La ventana ya lo traia: esto solo mueve la cuenta.
    void Commit(int64_t t) {
        if (!m_hasNext || t < IntroFrames() + m_nextAt) return;
        m_plan = m_next;
        m_origin = m_nextOrigin;
        m_hasNext = false;
    }

    // Que plan manda en la muestra `u` (tras el inicio), y desde que origen.
    bool UsesNext(int64_t u) const { return m_hasNext && u >= m_nextAt; }
    const Plan& PlanAt(int64_t u) const { return UsesNext(u) ? m_next : m_plan; }
    int64_t OriginAt(int64_t u) const { return UsesNext(u) ? m_nextOrigin : m_origin; }

    // --- El modo al azar ---------------------------------------------------

    // Empieza en `v` (la cuenta del plan, desde su origen): la primera parte
    // sigue en el compas que le toca hasta el final de su vuelta; desde ahi,
    // partes enteras desde su comienzo.
    void ResetShuffle(int64_t v) {
        m_segments.clear();
        m_bag.clear();
        m_shuffleFrom = std::max<int64_t>(0, v);
    }

    int NextFromBag(int avoid) {
        if (m_bag.empty()) {
            for (int i = 0; i < Parts(); ++i) m_bag.push_back(i);
            std::shuffle(m_bag.begin(), m_bag.end(), m_rng);
            // nunca la misma dos veces seguidas, tampoco entre una bolsa y la otra
            if (m_bag.size() > 1 && m_bag.back() == avoid) std::swap(m_bag.back(), m_bag.front());
        }
        const int part = m_bag.back();
        m_bag.pop_back();
        return part;
    }

    // El tramo del modo al azar que cubre `u` (linea de tiempo tras el inicio),
    // armando los que falten. Los tramos quedan guardados: armar la ventana otra
    // vez tiene que dar el MISMO orden, o el relevo cambiaria de parte.
    const Segment& SegmentAt(int64_t u) {
        if (m_segments.empty()) {
            const int first = NextFromBag(-1);
            const int64_t len = m_lengths[static_cast<size_t>(first)];
            // la primera arranca "en el compas": en la posicion que le toca por la linea de tiempo
            m_segments.push_back({m_shuffleFrom - (m_shuffleFrom % len), first});
        }
        while (u >= m_segments.back().start + m_lengths[static_cast<size_t>(m_segments.back().part)]) {
            const Segment& last = m_segments.back();
            const int64_t start = last.start + m_lengths[static_cast<size_t>(last.part)];
            m_segments.push_back({start, NextFromBag(last.part)});
        }
        // los que ya sonaron hace rato no hacen falta
        while (m_segments.size() > 2 && m_segments[1].start + m_maxLen * 2 < u) m_segments.erase(m_segments.begin());
        for (size_t i = m_segments.size(); i-- > 0;)
            if (m_segments[i].start <= u) return m_segments[i];
        return m_segments.front();
    }

    // Que suena en la muestra `t` de la linea de tiempo, segun el plan.
    Source SourceAt(int64_t t) {
        if (t < IntroFrames()) return {-1, t};
        const int64_t u = t - IntroFrames();
        const Plan& plan = PlanAt(u);
        const int64_t v = u - OriginAt(u);
        switch (plan.kind) {
            case Plan::Kind::Interleave: {
                const int64_t bar = m_unit / kBarsPerUnit;
                const int part = (v / (bar * kInterleaveBars)) % 2 == 0 ? plan.a : plan.b;
                return {part, v % m_lengths[static_cast<size_t>(part)]};
            }
            case Plan::Kind::Shuffle: {
                const Segment& seg = SegmentAt(v);
                return {seg.part, v - seg.start};
            }
            default: return {plan.a, v % m_lengths[static_cast<size_t>(plan.a)]};
        }
    }

    // Hasta donde (linea de tiempo) sigue sonando lo mismo que en `t`. El
    // cambio de nivel pendiente tambien es un cambio.
    int64_t NextChange(int64_t t) {
        if (t < IntroFrames()) return IntroFrames();
        const int64_t u = t - IntroFrames();
        const Plan& plan = PlanAt(u);
        const int64_t origin = OriginAt(u);
        const int64_t v = u - origin;
        int64_t next = INT64_MAX;
        switch (plan.kind) {
            case Plan::Kind::Interleave: {
                const int64_t seg = m_unit / kBarsPerUnit * kInterleaveBars;
                next = IntroFrames() + origin + (v / seg + 1) * seg;
                break;
            }
            case Plan::Kind::Shuffle: {
                const Segment& s = SegmentAt(v);
                next = IntroFrames() + origin + s.start + m_lengths[static_cast<size_t>(s.part)];
                break;
            }
            default: break;
        }
        if (m_hasNext && u < m_nextAt) next = std::min(next, IntroFrames() + m_nextAt);
        return next;
    }

    const int16_t* Frames(int part, int64_t pos) const {
        const size_t ch = static_cast<size_t>(m_intro.channels);
        if (part < 0) return m_intro.pcm.data() + static_cast<size_t>(pos) * ch;
        return m_parts[static_cast<size_t>(part)].pcm.data() + static_cast<size_t>(pos % m_lengths[static_cast<size_t>(part)]) * ch;
    }

    // Arma la ventana desde `t` y la pone a sonar en lugar de la que hay. Se
    // copia por TRAMOS (de un cambio de parte al siguiente), no muestra por
    // muestra preguntando que suena: en el Editor de Debug los scripts van sin
    // optimizar, y 1,2 millones de preguntas cada 13 s serian un tiron.
    void Build(int64_t t) {
        const size_t ch = static_cast<size_t>(m_intro.channels);
        m_windowFrames = m_maxLen * 2;
        std::vector<int16_t> pcm(static_cast<size_t>(m_windowFrames) * ch);
        int lastPart = -2;
        int64_t lastPos = 0; // posicion de la parte anterior en la ultima muestra de su tramo
        int64_t f = 0;
        while (f < m_windowFrames) {
            const Source src = SourceAt(t + f);
            const int64_t n = std::min(m_windowFrames - f, NextChange(t + f) - (t + f));
            // la copia, partida donde la parte da la vuelta
            int64_t done = 0;
            while (done < n) {
                const int64_t pos = src.pos + done;
                const int64_t len = src.part < 0 ? IntroFrames() : m_lengths[static_cast<size_t>(src.part)];
                const int64_t k = std::min(n - done, len - pos % len);
                std::memcpy(&pcm[static_cast<size_t>(f + done) * ch], Frames(src.part, pos), static_cast<size_t>(k) * ch * sizeof(int16_t));
                done += k;
            }
            // Cambio de parte: fundido cruzado de kRamp muestras, la que se va
            // sigue sonando en su posicion mientras baja. Del inicio a la
            // primera parte no: el inicio se termina, y empalma solo (medido).
            // Un salto dentro de la MISMA parte (un plan nuevo que la retoma
            // desde otro origen) tambien es un corte, y lleva el mismo fundido.
            const bool jump = lastPart >= 0 && src.part == lastPart &&
                              src.pos % m_lengths[static_cast<size_t>(src.part)] != (lastPos + 1) % m_lengths[static_cast<size_t>(src.part)];
            if (f > 0 && lastPart >= 0 && (src.part != lastPart || jump)) {
                for (int64_t i = 0; i < std::min<int64_t>(kRamp, n); ++i) {
                    const int16_t* old = Frames(lastPart, lastPos + 1 + i);
                    for (size_t c = 0; c < ch; ++c) {
                        int16_t& v = pcm[static_cast<size_t>(f + i) * ch + c];
                        v = static_cast<int16_t>((v * static_cast<int>(i) + old[c] * (kRamp - static_cast<int>(i))) / kRamp);
                    }
                }
            }
            lastPart = src.part;
            lastPos = src.pos + n - 1;
            f += n;
        }
        // El compensador: la ventana nueva al reves, por la rampa que le falta.
        std::vector<int16_t> comp(static_cast<size_t>(kRamp) * ch);
        for (size_t i = 0; i < static_cast<size_t>(kRamp); ++i)
            for (size_t c = 0; c < ch; ++c) {
                const int v = -static_cast<int>(pcm[i * ch + c]) * (kRamp - static_cast<int>(i)) / kRamp;
                comp[i * ch + c] = static_cast<int16_t>(std::clamp(v, -32768, 32767));
            }
        strace::AudioClipPtr clip = strace::MakeClipFromPcm16(pcm.data(), pcm.size(), static_cast<int>(ch), kRate);
        strace::AudioClipPtr compClip = strace::MakeClipFromPcm16(comp.data(), comp.size(), static_cast<int>(ch), kRate);
        strace::AudioMixer& mixer = strace::AudioService::Mixer();
        // Seguidas: lo que importa es que caigan en el mismo bloque del mezclador.
        const strace::VoiceId next = mixer.Play(clip, strace::AudioBus::Music, m_gain, false);
        const strace::VoiceId nextComp = mixer.Play(compClip, strace::AudioBus::Music, m_gain, false);
        StopVoice(m_voice);
        StopVoice(m_comp);
        m_voice = next;
        m_comp = nextComp;
        m_windowStart = t;
        if (gameaudio::LogPlays()) {
            const wchar_t* kind = m_plan.kind == Plan::Kind::Shuffle ? L"al azar" : m_plan.kind == Plan::Kind::Interleave ? L"intercaladas" : L"una";
            strace::Log::Info(L"GameMusic: ventana desde el segundo " + Seconds(t) + L", " + kind + L" (partes " +
                              std::to_wstring(m_plan.a + 1) + L"/" + std::to_wstring(m_plan.b + 1) + L")");
        }
    }

    gameaudio::WavPcm m_intro;
    std::vector<gameaudio::WavPcm> m_parts;
    std::vector<int64_t> m_lengths; // de cada parte, en muestras: multiplo de m_unit
    int64_t m_unit = 0;             // el largo de la parte mas corta
    int64_t m_maxLen = 0;
    bool m_loaded = false;
    float m_gain = 1.0f;
    Plan m_plan;
    int64_t m_origin = 0;           // desde donde (tras el inicio) cuenta m_plan su compas 1
    bool m_hasNext = false;         // un cambio de nivel esperando su compas
    Plan m_next;
    int64_t m_nextAt = 0;           // tras el inicio: donde empieza a sonar m_next
    int64_t m_nextOrigin = 0;
    int64_t m_windowStart = 0;     // en que muestra de la linea de tiempo empieza la voz que suena
    int64_t m_windowFrames = 0;
    std::vector<Segment> m_segments;
    std::vector<int> m_bag;
    int64_t m_shuffleFrom = 0;
    std::mt19937 m_rng{std::random_device{}()};
    strace::VoiceId m_voice = strace::kInvalidVoice;
    strace::VoiceId m_comp = strace::kInvalidVoice;
};

// Lo que mira el estado. Vive dentro de PuzzleDirector (un miembro, llamado al
// principio de su OnUpdate): el director corre en el menu y en la partida, y
// asi no hace falta otro objeto en la escena.
class GameAudio {
public:
    // Volumen de cada cosa, en dB sobre lo que trae el archivo.
    //
    // La musica de la partida (FL Studio) viene masterizada fuerte: RMS de
    // -11,5 dB, picos a 0. Los efectos, sintetizados, rondan -24 de RMS pero
    // con picos de -7: son golpes. Con la musica a -12 quedaban los dos al
    // mismo nivel promedio, y los golpes se comian la musica ("los efectos
    // DEMASIADO fuertes y la musica casi ni se escucha", 22/10/2026). Ahora la
    // musica sube y TODOS los efectos bajan juntos por el bus Sfx, que mantiene
    // el equilibrio que ya tenian entre ellos. Segunda vuelta (la musica
    // "todavia opacada"): otros 4 dB de efectos y 2 de musica, y los dos
    // BUCLES mucho mas abajo: suenan todo el rato que la luz viaja, en el
    // registro medio donde vive la melodia, y eso tapa mas que cualquier golpe.
    static constexpr float kMusicDb = -2.0f;
    static constexpr float kSfxBusDb = -12.0f;
    static constexpr float kHumDb = -10.0f;
    static constexpr float kOrbitDb = -8.0f;
    static constexpr float kFadeSeconds = 0.6f;

    void Start() {
        gameaudio::LoadAll();
        m_gameMusic.Load();
        gameaudio::Queue().clear();
        strace::AudioService::SetBusVolumeDb(strace::AudioBus::Music, 0.0f);
        strace::AudioService::SetBusVolumeDb(strace::AudioBus::Sfx, kSfxBusDb);
    }

    // Al Detener: lo que este script puso, lo saca. La musica la puso el; los
    // bucles tambien. Nada mas (nadie para una pista que no puso).
    void Stop() {
        StopVoice(m_hum);
        StopVoice(m_orbit);
        m_gameMusic.Stop();
        if (m_music == Music::Menu) strace::AudioService::StopTrack();
        m_music = Music::None;
        // los buses son del proceso: el Editor sigue vivo despues de Detener
        strace::AudioService::SetBusVolumeDb(strace::AudioBus::Music, 0.0f);
        strace::AudioService::SetBusVolumeDb(strace::AudioBus::Sfx, 0.0f);
        gameaudio::Queue().clear();
    }

    void Tick(float dt) {
        const GameFlow& flow = Flow();
        const PuzzleSession& s = Session();
        TickQueue(dt);
        TickMusic(dt, flow);
        if (m_music == Music::Game) {
            m_gameMusic.SetLevel(flow.currentLevel, flow.levelCount);
            m_gameMusic.Tick();
        }

        // El paso entre niveles: cada etapa nueva tiene su sonido.
        if (flow.passage.stage != m_stage) {
            m_stage = flow.passage.stage;
            if (m_stage == PassageStage::Out) gameaudio::Play("Flow/level_collapse");
            if (m_stage == PassageStage::Card) gameaudio::Play("Flow/level_card");
            if (m_stage == PassageStage::In) gameaudio::Play("Flow/level_grow");
        }
        if (flow.finishedAll && !m_finished) gameaudio::Play("Flow/game_complete", -1.0f);
        m_finished = flow.finishedAll;

        TickLight(flow, s);
    }

private:
    enum class Music { None, Menu, Game };

    static void StopVoice(strace::VoiceId& v) {
        if (v != strace::kInvalidVoice) strace::AudioService::Stop(v);
        v = strace::kInvalidVoice;
    }

    void TickQueue(float dt) {
        auto& q = gameaudio::Queue();
        for (size_t i = 0; i < q.size();) {
            q[i].delay -= dt;
            if (q[i].delay <= 0.0f) {
                gameaudio::Play(q[i].name, q[i].db);
                q.erase(q.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
    }

    // Una sola pista (el motor tiene una), asi que menu -> partida no es un
    // cruce sino un relevo: se baja el bus, se cambia la pista y se sube. Se
    // hace aqui a mano y no con FadeBus porque el cambio puede pedirse otra
    // vez a mitad del fundido (Esc en el primer segundo de un nivel), y una
    // corrutina por pedido serian dos fundidos peleando por el mismo bus.
    void TickMusic(float dt, const GameFlow& flow) {
        const Music want = flow.InGame() ? Music::Game : Music::Menu;
        if (m_music == Music::None) {
            // La primera: sin nada que bajar, entra subiendo.
            Begin(want);
            m_fade = 0.0f;
        }
        // Si se vuelve a pedir la que suena a mitad de la bajada, la bajada se
        // da vuelta en vez de reempezar la pista.
        m_fadingOut = want != m_music;
        if (m_fadingOut) {
            m_fade = std::max(0.0f, m_fade - dt / kFadeSeconds);
            if (m_fade <= 0.0f) {
                Begin(want);
                m_fadingOut = false;
            }
        } else {
            m_fade = std::min(1.0f, m_fade + dt / (kFadeSeconds * 2.0f));
        }
        // En dB: de -40 (casi nada) a 0, con la curva lineal en dB que es pareja al oido.
        strace::AudioService::SetBusVolumeDb(strace::AudioBus::Music, -40.0f * (1.0f - m_fade));
    }

    // El menu es una pista en streaming; la partida, la musica por partes
    // (GameMusic). Cada una apaga a la otra al entrar.
    void Begin(Music m) {
        m_music = m;
        if (m == Music::Game) {
            strace::AudioService::StopTrack();
            m_gameMusic.Begin(Flow().currentLevel, Flow().levelCount, kMusicDb);
            return;
        }
        m_gameMusic.Stop();
        const std::wstring path = strace::SceneService::ProjectRoot() + L"/Assets/Audio/Music/music_menu.ogg";
        strace::AudioService::PlayTrackFile(path, kMusicDb, true);
        if (gameaudio::LogPlays()) strace::Log::Info(L"GameAudio: musica " + path);
    }

    // LA LUZ: lo que hace se lee del recorrido ya calculado (s.beam) y de
    // cuanto se revelo (s.progress). Cada tramo suena cuando la punta de la
    // luz LLEGA a el - no cuando se calcula -, asi que pausar, PASO o la
    // velocidad de las opciones no necesitan nada propio: el sonido sigue a la
    // imagen.
    void TickLight(const GameFlow& flow, const PuzzleSession& s) {
        const bool active = flow.InGame() && s.loaded && !flow.passage.Active();

        // Recorrido nuevo: una jugada, deshacer, reiniciar u otro nivel. La
        // revision sola no alcanza: cada nivel nace con una sesion nueva, y su
        // primera revision es el mismo numero que la del nivel anterior. Que la
        // luz vuelva para atras si que dice siempre "empezo otra".
        if (s.revision != m_revision || s.progress < m_progress - 1e-4f || s.levelIndex != m_level) {
            m_revision = s.revision;
            m_level = s.levelIndex;
            m_hop = 1;          // el tramo 0 es el plano de salida: ahi nace, eso es light_emit
            m_figures = 0;
            m_emitted = false;
            m_ended = false;
            StopVoice(m_orbit);
        }
        m_progress = s.progress;

        if (!active) {
            StopVoice(m_hum);
            StopVoice(m_orbit);
            m_paused = s.paused;
            return;
        }

        if (s.paused != m_paused) {
            if (!gameaudio::SuppressPauseSound()) gameaudio::Play(s.paused ? "Game/pause" : "Game/resume");
            m_paused = s.paused;
        }
        gameaudio::SuppressPauseSound() = false;

        if (!m_emitted && s.progress > 0.0f) {
            m_emitted = true;
            gameaudio::Play("Light/light_emit");
        }

        const auto& hops = s.beam.hops;
        while (m_hop < hops.size() && hops[m_hop].s0 <= s.progress) {
            // BuildBeamPath arma un tramo de dibujo por cada tramo del trazado,
            // en el mismo orden: si llego por una arista lo sabe el trazado.
            const bool viaEdge = m_hop < s.trace.hops.size() && s.trace.hops[m_hop].fromEdge;
            const DrawHop& h = hops[m_hop++];
            if (h.onPlane) {
                if (viaEdge) gameaudio::Play("Light/light_edge");
                continue;
            }
            switch (h.shape) {
                case Shape::Wormhole:
                    if (!h.outMouth) gameaudio::Play("Light/wormhole"); // el tunel entero suena al entrar
                    break;
                case Shape::WhiteHole: gameaudio::Play("Light/whitehole_return"); break;
                case Shape::BlackHole: break; // es un final: lo dice TraceEnd::Absorbed
                default:
                    // Cada figura que cruza sube un grado de la escala: un
                    // recorrido largo se oye como una frase que va a algun lado.
                    m_figures = std::min(m_figures + 1, 5);
                    gameaudio::Play("Light/light_enter_" + std::to_string(m_figures));
                    break;
            }
        }

        const bool travelling = s.progress < s.beam.length && !s.paused;
        if (travelling && m_hum == strace::kInvalidVoice) m_hum = gameaudio::Play("Light/light_hum_loop", kHumDb, true);
        if (!travelling) StopVoice(m_hum);

        if (!m_ended && s.beam.length > 0.0f && s.FullyRevealed()) {
            m_ended = true;
            switch (s.trace.end) {
                case TraceEnd::Reached: gameaudio::Play("Light/solved"); break;
                case TraceEnd::Lost: gameaudio::Play("Light/light_lost"); break;
                case TraceEnd::Blocked: gameaudio::Play("Light/light_blocked"); break;
                case TraceEnd::Absorbed: gameaudio::Play("Light/blackhole_fall"); break;
                case TraceEnd::Orbit:
                case TraceEnd::Loop: m_orbit = gameaudio::Play("Light/light_orbit_loop", kOrbitDb, true); break;
            }
        }
    }

    Music m_music = Music::None;
    GameMusic m_gameMusic;
    float m_fade = 0.0f;
    bool m_fadingOut = false;

    PassageStage m_stage = PassageStage::None;
    bool m_finished = false;

    int m_revision = -1;
    int m_level = -1;
    float m_progress = 0.0f;
    size_t m_hop = 1;
    int m_figures = 0;
    bool m_emitted = false;
    bool m_ended = false;
    bool m_paused = false;
    strace::VoiceId m_hum = strace::kInvalidVoice;
    strace::VoiceId m_orbit = strace::kInvalidVoice;
};

} // namespace gravitational
