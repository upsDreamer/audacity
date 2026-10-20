// 基准：SqliteSampleBlock 等价复刻（用户裁决 2026-10-20）。
// 真实 SqliteSampleBlock 依赖 DBConnection/WaveTrack 全链（判定拆不开），
// 此处按其真实写入路径等价复刻：相同 PRAGMA、相同表结构、相同 summary 布局、
// 相同"每块单条 INSERT bind_blob"协议。标注：等价复刻，非真实代码。
// 事实依据：au3-project-file-io/SqliteSampleBlock.cpp:744-811（INSERT）、
// DBConnection.cpp:27-42（页 64KB/WAL/NORMAL/无自动检查点）；
// 块 262144 float 样本（Sequence.cpp:56-57）；summary 两级 min/max/RMS。
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "sqlite3.h"

#include "../common/bench_util.h"

namespace {

size_t Failures = 0;

void Check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        ++Failures;
    }
}

constexpr size_t kMaxSamples = 262144;      // float 轨块上限（Sequence.cpp:156-157）
constexpr size_t kBlockSizeBytes = kMaxSamples * 4;
constexpr int kFrames256 = int(kMaxSamples / 256);
constexpr int kFrames64k = int(kMaxSamples / 65536);

// 与 SqliteSampleBlock::CalcSummary 等价的两级 min/max/RMS（3 float × 每帧）
struct Summary
{
    // 显式构造函数：类内用花括号会走 initializer_list（单元素）陷阱，
    // 用圆括号会是最令人头痛的解析（函数声明）
    Summary()
        : s256(size_t(kFrames256) * 3)
        , s64k(size_t(kFrames64k) * 3)
    {}

    std::vector<float> s256;
    std::vector<float> s64k;
};

Summary CalcSummary(const float* data, size_t len)
{
    Summary s;
    for (size_t f = 0; f < len / 256; ++f) {
        float mn = data[f * 256], mx = mn, sq = 0.0f;
        for (size_t i = 0; i < 256; ++i) {
            const float v = data[f * 256 + i];
            mn = std::min(mn, v);
            mx = std::max(mx, v);
            sq += v * v;
        }
        s.s256[f * 3] = mn;
        s.s256[f * 3 + 1] = mx;
        s.s256[f * 3 + 2] = std::sqrt(sq / 256.0f);
    }
    // 64k 帧由 256 帧聚合（真实实现同理）
    for (size_t f = 0; f < len / 65536; ++f) {
        float mn = 1e30f, mx = -1e30f, sq = 0.0f;
        for (size_t k = 0; k < 256; ++k) {
            mn = std::min(mn, s.s256[(f * 256 + k) * 3]);
            mx = std::max(mx, s.s256[(f * 256 + k) * 3 + 1]);
            sq += s.s256[(f * 256 + k) * 3 + 2] * s.s256[(f * 256 + k) * 3 + 2] * 256.0f;
        }
        s.s64k[f * 3] = mn;
        s.s64k[f * 3 + 1] = mx;
        s.s64k[f * 3 + 2] = std::sqrt(sq / 65536.0f);
    }
    return s;
}

void ExecOrDie(sqlite3* db, const char* sql)
{
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::fprintf(stderr, "SQL 失败: %s (%s)\n", sql, err ? err : "?");
        std::exit(1);
    }
}

// 一种日志模式的完整写路径
struct WriteStats
{
    double nsPerSample = 0;
    double mbPerSec = 0;
    long long walBytes = 0;
    long long dbBytes = 0;
};

WriteStats BenchWrite(const char* path, const char* pragmas, const float* data, size_t numBlocks)
{
    std::filesystem::remove(path);
    std::filesystem::remove(std::string(path) + "-wal");
    std::filesystem::remove(std::string(path) + "-shm");

    sqlite3* db = nullptr;
    sqlite3_open(path, &db);
    ExecOrDie(db, pragmas);
    ExecOrDie(db,
        "CREATE TABLE sampleblocks ("
        "blockid INTEGER PRIMARY KEY AUTOINCREMENT, "
        "sampleformat INTEGER, summin REAL, summax REAL, sumrms REAL, "
        "summary256 BLOB, summary64k BLOB, samples BLOB)");

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db,
        "INSERT INTO sampleblocks(sampleformat, summin, summax, sumrms, summary256, summary64k, samples) "
        "VALUES(?,?,?,?,?,?,?)", -1, &stmt, nullptr);

    using clock = std::chrono::steady_clock;
    const size_t totalSamples = numBlocks * kMaxSamples;
    for (int i = 0; i < 3; ++i) { // 预热
        sqlite3_bind_int(stmt, 1, 3);
        sqlite3_bind_double(stmt, 2, -0.5);
        sqlite3_bind_double(stmt, 3, 0.5);
        sqlite3_bind_double(stmt, 4, 0.3);
        sqlite3_bind_blob(stmt, 5, nullptr, 0, SQLITE_STATIC);
        sqlite3_bind_blob(stmt, 6, nullptr, 0, SQLITE_STATIC);
        sqlite3_bind_blob(stmt, 7, data, int(kBlockSizeBytes), SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }

    const auto t0 = clock::now();
    for (size_t b = 0; b < numBlocks; ++b) {
        const Summary s = CalcSummary(data, kMaxSamples);
        double mn = s.s256[0], mx = s.s256[1], sq = 0.0;
        for (int f = 0; f < kFrames256; ++f) {
            mn = std::min(mn, double(s.s256[f * 3]));
            mx = std::max(mx, double(s.s256[f * 3 + 1]));
            sq += double(s.s256[f * 3 + 2]) * s.s256[f * 3 + 2] * 256.0;
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        sqlite3_bind_int(stmt, 1, 3); // floatSample
        sqlite3_bind_double(stmt, 2, mn);
        sqlite3_bind_double(stmt, 3, mx);
        sqlite3_bind_double(stmt, 4, std::sqrt(sq / double(kMaxSamples)));
        sqlite3_bind_blob(stmt, 5, s.s256.data(), int(s.s256.size() * sizeof(float)), SQLITE_STATIC);
        sqlite3_bind_blob(stmt, 6, s.s64k.data(), int(s.s64k.size() * sizeof(float)), SQLITE_STATIC);
        sqlite3_bind_blob(stmt, 7, data, int(kBlockSizeBytes), SQLITE_STATIC);
        sqlite3_step(stmt);
    }
    const auto t1 = clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    sqlite3_finalize(stmt);

    WriteStats st;
    st.nsPerSample = ms * 1e6 / double(totalSamples);
    st.mbPerSec = double(totalSamples * 4) / (ms * 1e3);
    st.walBytes = (long long) std::filesystem::exists(std::string(path) + "-wal")
        ? (long long) std::filesystem::file_size(std::string(path) + "-wal") : 0;
    st.dbBytes = (long long) std::filesystem::file_size(path);
    sqlite3_close(db);
    return st;
}

} // namespace

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0); // 段错误时不丢输出
    std::printf("bench_sqlite_block — SqliteSampleBlock 写/读路径等价复刻（真实 sqlite3 3.45，MSVC /O2）\n");
    std::printf("（非真实代码：按 SqliteSampleBlock.cpp 的 PRAGMA/表结构/summary 布局/单条 INSERT 协议复刻）\n");

    auto data = bench::MakeSignal(kMaxSamples, 0.8f);
    const size_t numBlocks = 64; // 64MB 音频数据

    bench::Header("正确性验证");
    {
        // 写入后读回一致 + summary 与直接计算一致
        const WriteStats st = BenchWrite("bench_sqlite_block.db",
            "PRAGMA page_size=65536;"
            "PRAGMA journal_mode=WAL;"
            "PRAGMA synchronous=NORMAL;"
            "PRAGMA wal_autocheckpoint=0;", data.data(), 2);
        sqlite3* db = nullptr;
        if (sqlite3_open("bench_sqlite_block.db", &db) != SQLITE_OK) {
            std::fprintf(stderr, "open 失败\n");
            return 1;
        }
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT samples FROM sampleblocks WHERE blockid=1",
                               -1, &stmt, nullptr) != SQLITE_OK) {
            std::fprintf(stderr, "prepare 失败: %s\n", sqlite3_errmsg(db));
            return 1;
        }
        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW) {
            std::fprintf(stderr, "step rc=%d（预期 100=ROW）\n", rc);
            return 1;
        }
        const auto* blob = static_cast<const float*>(sqlite3_column_blob(stmt, 0));
        const int bytes = sqlite3_column_bytes(stmt, 0);
        if (!blob || bytes != int(kBlockSizeBytes)) {
            std::fprintf(stderr, "blob=%p bytes=%d\n", (const void*) blob, bytes);
            return 1;
        }
        Check(true, "读回 BLOB 尺寸一致");
        float maxErr = 0.0f;
        for (size_t i = 0; i < kMaxSamples; ++i) {
            maxErr = std::max(maxErr, std::fabs(blob[i] - data[i]));
        }
        Check(maxErr == 0.0f, "读回内容与写入逐位一致（无压缩直存）");
        sqlite3_finalize(stmt);
        sqlite3_close(db);

        const Summary s = CalcSummary(data.data(), kMaxSamples);
        float mn = data[0], mx = data[0];
        for (size_t i = 0; i < 256; ++i) { // 首帧
            mn = std::min(mn, data[i]);
            mx = std::max(mx, data[i]);
        }
        Check(std::fabs(s.s256[0] - mn) < 1e-6f && std::fabs(s.s256[1] - mx) < 1e-6f,
              "summary256 首帧 min/max 与直接计算一致");
    }

    if (Failures) {
        std::printf("\n%zu 项验证失败，跳过计时。\n", Failures);
        return 1;
    }

    bench::Header("写路径（64MB 音频 = 64 块 × 1MB，含 summary 计算）");
    {
        const WriteStats st = BenchWrite("bench_sqlite_block_wal.db",
            "PRAGMA page_size=65536;"
            "PRAGMA journal_mode=WAL;"
            "PRAGMA synchronous=NORMAL;"
            "PRAGMA wal_autocheckpoint=0;", data.data(), numBlocks);
        char buf[80];
        std::snprintf(buf, sizeof(buf), "已保存项目模式（WAL + NORMAL）：%0.0f MB/s", st.mbPerSec);
        bench::Report(buf, st.nsPerSample);
        std::printf("      WAL 瞬时 %lld MB / 主库 %lld MB → 写期间磁盘占用 ≈ %.2f×\n",
                    st.walBytes >> 20, st.dbBytes >> 20,
                    double(st.walBytes + st.dbBytes)
                        / double(numBlocks * kBlockSizeBytes));
    }
    {
        const WriteStats st = BenchWrite("bench_sqlite_block_off.db",
            "PRAGMA page_size=65536;"
            "PRAGMA journal_mode=OFF;"
            "PRAGMA synchronous=OFF;", data.data(), numBlocks);
        char buf[80];
        std::snprintf(buf, sizeof(buf), "临时/未保存模式（journal OFF + OFF）：%0.0f MB/s", st.mbPerSec);
        bench::Report(buf, st.nsPerSample);
    }

    bench::Header("读路径（顺序逐块 SELECT + memcpy）");
    {
        sqlite3* db = nullptr;
        sqlite3_open("bench_sqlite_block_wal.db", &db);
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db, "SELECT samples FROM sampleblocks WHERE blockid=?", -1, &stmt, nullptr);
        std::vector<float> out(kMaxSamples);
        const double ns = bench::NsPerOp(numBlocks * kMaxSamples, [&] {
            for (size_t b = 1; b <= numBlocks; ++b) {
                sqlite3_reset(stmt);
                sqlite3_bind_int64(stmt, 1, sqlite3_int64(b));
                sqlite3_step(stmt);
                std::memcpy(out.data(), sqlite3_column_blob(stmt, 0), kBlockSizeBytes);
            }
        }, 50.0);
        bench::Report("块读出 + 转 float 目标（memcpy 等价）", ns);
        sqlite3_finalize(stmt);
        sqlite3_close(db);
    }

    std::printf("\n完成。\n");
    return 0;
}
