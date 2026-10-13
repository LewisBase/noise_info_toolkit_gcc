// /tmp/t_wav.cpp — v3.3.4 验收 · 任务2
// 用 v3.3.4 的 NoiseProcessor 逐秒处理 261010 的 Pa-WAV，输出 LAeq/LCeq/LZeq 供与设备 CSV 对比。
// 编译： g++ -O2 -Iinclude /tmp/t_wav.cpp build_test/libnoise_toolkit.a -o /tmp/t_wav
// 用法： ./t_wav /tmp/wav_10.f32 48000
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
#include <string>
#include "noise_processor.hpp"

using namespace noise_toolkit;

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: t_wav <raw_f32_path> [fs]\n"); return 2; }
    const std::string path = argv[1];
    const int FS = (argc >= 3) ? std::atoi(argv[2]) : 48000;

    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); return 2; }

    const int BLK = FS;                      // 1 秒一块，与设备 CSV 的 duration_s=1 对齐
    std::vector<float> buf(BLK);
    NoiseProcessor proc(FS);

    std::printf("idx,LAeq_dB,LCeq_dB,LZeq_dB,LAFmax_dB,LCpeak_dB\n");
    long idx = 0;
    for (;;) {
        size_t got = std::fread(buf.data(), sizeof(float), BLK, f);
        if (got == 0) break;
        if (got < static_cast<size_t>(BLK)) {
            // 末尾不足 1 秒：补零到整块再算（与原 CSV 行数保持一一对应）
            std::vector<float> pad(BLK, 0.0f);
            for (size_t i = 0; i < got; ++i) pad[i] = buf[i];
            auto m = proc.process_segment(pad.data(), pad.data() + BLK, 1.0f);
            std::printf("%ld,%.4f,%.4f,%.4f,%.4f,%.4f\n",
                        idx++, m.LAeq, m.LCeq, m.LZeq, m.LAFmax, m.LCPeak);
            break;
        }
        auto m = proc.process_segment(buf.data(), buf.data() + BLK, 1.0f);
        std::printf("%ld,%.4f,%.4f,%.4f,%.4f,%.4f\n",
                    idx++, m.LAeq, m.LCeq, m.LZeq, m.LAFmax, m.LCPeak);
    }
    std::fclose(f);
    return 0;
}
