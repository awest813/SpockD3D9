/**
 * DXSO shader-corpus harness — batch-compiles real D3D9 shader bytecode
 * through SpockD3D9's own DXSO -> SPIR-V compiler, with no GPU or Vulkan
 * loader required.
 *
 * Motivation (see docs/DX9_METAL_ROADMAP.md): an external D3D9 -> Metal
 * project (dx9mt) validated its translator against Fallout: New Vegas's
 * ~15,535 shipped shaders and found a defect affecting 22% of instructions
 * — a class of bug invisible to hand-written smoke shaders. SpockD3D9's
 * DXSO layer sees only a handful of probe shaders before its first retail
 * run; this tool closes that gap ahead of Milestone F.
 *
 * Inputs:
 *   --selftest        run the built-in hand-assembled fixture shaders
 *                     (SM2 + SM3; includes non-prefix write masks — the
 *                     miscompile class dx9mt found). Always CI-runnable.
 *   --sdp PATH        scan Bethesda-style shader packages (*.sdp): raw
 *                     token streams where each shader starts at a version
 *                     token (0xFFFExxxx VS / 0xFFFFxxxx PS) and ends at
 *                     the 0x0000FFFF end token. Point at a legally-owned
 *                     game's Data/Shaders directory, e.g.
 *                       dxso-corpus --sdp "$FNV_DIR/Data/Shaders"
 *   --dump PATH       scan DXVK_SHADER_DUMP_PATH dumps (*.sm3_dxbc, one
 *                     raw DXSO shader per file — collected from any real
 *                     title run or the d3d9-gamebryo-probe).
 *   --report FILE     write per-failure details + histogram to FILE
 *
 * Exit codes: 0 = every bounded shader compiled; 1 = at least one bounded
 * shader failed to compile; 2 = usage / IO error. Unbounded scan candidates
 * (version token with no end token in range — expected noise inside .sdp
 * packages) are counted separately and never fail the run.
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "../../src/dxso/dxso_module.h"
#include "../../src/dxso/dxso_modinfo.h"
#include "../../src/dxso/dxso_reader.h"
#include "../../src/d3d9/d3d9_constant_layout.h"
#include "../../src/util/util_error.h"

namespace fs = std::filesystem;

namespace {

  // Canonical non-SWVP constant layouts (D3D9DeviceEx::DetermineConstantLayouts,
  // d3d9_device.cpp; caps values from d3d9_caps.h).
  dxvk::D3D9ConstantLayout vertexLayout() {
    dxvk::D3D9ConstantLayout layout = { };
    layout.floatCount   = 256; // caps::MaxFloatConstantsVS
    layout.intCount     = 16;  // caps::MaxOtherConstants
    layout.boolCount    = 16;
    layout.bitmaskCount = 1;   // align(16, 32) / 32
    return layout;
  }

  dxvk::D3D9ConstantLayout pixelLayout() {
    dxvk::D3D9ConstantLayout layout = { };
    layout.floatCount   = 224; // caps::MaxSM3FloatConstantsPS
    layout.intCount     = 16;
    layout.boolCount    = 16;
    layout.bitmaskCount = 1;
    return layout;
  }

  struct Stats {
    uint64_t bounded   = 0; // version token + end token found
    uint64_t passed    = 0;
    uint64_t failed    = 0;
    uint64_t unbounded = 0; // version token with no end token in range (noise)
    std::map<std::string, uint64_t> failureHistogram;

    void recordFailure(const std::string& sig) {
      failed++;
      // Keep histogram keys bounded
      failureHistogram[sig.substr(0, 100)]++;
    }
  };

  Stats g_stats;

  std::string exceptionSignature(const std::exception& e) {
    std::string msg = e.what();
    // Collapse volatile suffixes (offsets, register numbers) in messages
    return msg.substr(0, msg.find('\n'));
  }

  // Compile one DXSO shader (version token .. end token, both included).
  bool compileBlob(const uint32_t* words, size_t wordCount, const std::string& name) {
    g_stats.bounded++;

    try {
      dxvk::DxsoReader reader(reinterpret_cast<const char*>(words));
      dxvk::DxsoModule module(reader);

      dxvk::DxsoAnalysisInfo analysis = module.analyze();

      const bool isVertex =
        module.info().shaderStage() == VK_SHADER_STAGE_VERTEX_BIT;

      dxvk::DxsoModuleInfo moduleInfo = { };
      moduleInfo.options.d3d9FloatEmulation            = dxvk::D3D9FloatEmulation::Disabled;
      moduleInfo.options.forceSamplerTypeSpecConstants = false;
      moduleInfo.options.forceSampleRateShading        = false;
      moduleInfo.options.vertexFloatConstantBufferAsSSBO = false;
      moduleInfo.options.sincosEmulation               = false;

      dxvk::Rc<dxvk::DxvkShader> shader = module.compile(
        moduleInfo, name, analysis,
        isVertex ? vertexLayout() : pixelLayout());

      if (shader == nullptr) {
        g_stats.recordFailure("compile returned null");
        return false;
      }

      g_stats.passed++;
      return true;
    } catch (const dxvk::DxvkError& e) {
      g_stats.recordFailure(std::string("DxvkError: ") + e.message());
      return false;
    } catch (const std::exception& e) {
      g_stats.recordFailure(exceptionSignature(e));
      return false;
    }
  }

  // ---- .sdp package scanning ---------------------------------------------
  //
  // A .sdp is a raw stream of tokens. Real shaders begin at a version token
  // (high half 0xFFFE = vertex, 0xFFFF = pixel, low byte pair = major/minor
  // 1.x-3.x) and terminate at the 0x0000FFFF end token. Only bounded
  // [version .. end] ranges are compiled; the DXSO parser stops at the end
  // token by format contract, so it never runs off the blob.

  constexpr size_t kMaxBlobWords = 256 * 1024; // ~1 MB; FNV's largest are ~64 KB

  bool isVersionToken(uint32_t t) {
    const uint32_t high = t >> 16;
    if (high != 0xFFFE && high != 0xFFFF)
      return false;
    // Low word: major in bits 8-15, minor in bits 0-7 (e.g. vs_2_0 = 0xFFFE0200)
    const uint32_t major = (t >> 8) & 0xFF;
    return major >= 1 && major <= 3;
  }

  // Guard padding appended to loaded buffers (see loadWords): the scanner
  // must ignore it so a trailing version token still counts as unbounded
  // noise instead of compiling an empty shader against the padding.
  constexpr size_t kLoadPadWords = 8;

  std::vector<uint32_t> loadWords(const fs::path& path, bool& ok) {
    ok = false;
    std::ifstream file(path, std::ios::binary);
    if (!file)
      return { };

    std::vector<char> bytes(
      (std::istreambuf_iterator<char>(file)),
      std::istreambuf_iterator<char>());

    // Token streams are whole u32s; a ragged tail cannot contain a shader.
    const size_t wordCount = bytes.size() / 4;
    // Guard padding: the DXSO decoder walks to the next end token with no
    // blob bound, so a malformed final shader must not run off the buffer.
    // 0x0000FFFF is never a version token, so the scanner ignores it.
    std::vector<uint32_t> words(wordCount + kLoadPadWords, 0x0000FFFF);
    if (wordCount)
      std::memcpy(words.data(), bytes.data(), wordCount * 4);

    ok = true;
    return words;
  }

  void scanSdpFile(const fs::path& path) {
    bool ok = false;
    std::vector<uint32_t> words = loadWords(path, ok);
    if (!ok) {
      std::fprintf(stderr, "dxso-corpus: cannot open %s\n", path.string().c_str());
      return;
    }
    // Scan only the file's own words; the appended guard padding is for
    // decoder safety, not for blob accounting.
    const size_t count = words.size() - kLoadPadWords;

    size_t i = 0;
    size_t compiledInFile = 0;
    while (i < count) {
      if (!isVersionToken(words[i])) {
        i++;
        continue;
      }

      size_t end = i + 1;
      while (end < count && end - i < kMaxBlobWords && words[end] != 0x0000FFFF)
        end++;

      if (end >= count || end - i >= kMaxBlobWords) {
        // No end token in range: scan noise, not a shader. Skip the token.
        g_stats.unbounded++;
        i++;
        continue;
      }

      const std::string name =
        path.filename().string() + "[" + std::to_string(i) + "]";
      compileBlob(&words[i], end - i + 1, name);
      compiledInFile++;
      i = end + 1;
    }

    std::printf("dxso-corpus: %s — %zu shader(s)\n",
                path.string().c_str(), compiledInFile);
  }

  void scanSdpPath(const fs::path& path) {
    if (fs::is_regular_file(path)) {
      scanSdpFile(path);
      return;
    }
    for (const auto& entry : fs::recursive_directory_iterator(path)) {
      if (entry.is_regular_file() && entry.path().extension() == ".sdp")
        scanSdpFile(entry.path());
    }
  }

  // ---- DXVK_SHADER_DUMP_PATH dump scanning -------------------------------

  void scanDumpFile(const fs::path& path) {
    bool ok = false;
    std::vector<uint32_t> words = loadWords(path, ok);
    if (!ok) {
      std::fprintf(stderr, "dxso-corpus: cannot open %s\n", path.string().c_str());
      return;
    }
    if (words.size() < 2) {
      std::fprintf(stderr, "dxso-corpus: skipping truncated dump %s\n",
                   path.string().c_str());
      return;
    }

    compileBlob(words.data(), words.size(), path.filename().string());
  }

  void scanDumpPath(const fs::path& path) {
    if (fs::is_regular_file(path)) {
      scanDumpFile(path);
      return;
    }
    for (const auto& entry : fs::recursive_directory_iterator(path)) {
      if (entry.is_regular_file() && entry.path().extension() == ".sm3_dxbc")
        scanDumpFile(entry.path());
    }
  }

  // ---- Built-in fixtures --------------------------------------------------
  //
  // The SM2 shaders are the same hand-assembled bytecode the
  // d3d9-gamebryo-probe draws with in CI. The write-mask and sincos fixtures
  // target the non-prefix write-mask class (dx9mt's 22%-of-instructions bug)
  // and sincos handling. SM3 covers texldd (explicit gradients).

  struct Fixture {
    const char*             name;
    std::vector<uint32_t>   code;
  };

  const std::vector<Fixture>& fixtures() {
    static const std::vector<Fixture> list = {
      { "vs_2_0-passthrough", {
        0xFFFE0200,                                       // vs_2_0
        0x0200001F, 0x80000000, 0x900F0000,               // dcl_position v0
        0x02000001, 0xC00F0000, 0x90E40000,               // mov oPos, v0
        0x02000001, 0xE00F0000, 0x90E40000,               // mov oT0, v0
        0x0000FFFF,                                       // end
      } },
      { "ps_2_0-solid", {
        0xFFFF0200,                                       // ps_2_0
        0x05000051, 0xA00F0000,                           // def c0, 1, 0, 0, 1
          0x3F800000, 0x00000000, 0x00000000, 0x3F800000,
        0x02000001, 0x800F0000, 0xA0E40000,               // mov r0, c0
        0x02000001, 0x800F0800, 0x80E40000,               // mov oC0, r0
        0x0000FFFF,                                       // end
      } },
      { "ps_2_0-texld", {
        0xFFFF0200,                                       // ps_2_0
        0x0200001F, 0x80000000, 0xB00F0000,               // dcl t0
        0x0200001F, 0x90000000, 0xA00F0800,               // dcl_2d s0
        0x03000042, 0x800F0000, 0xB0E40000, 0xA0E40800,   // texld r0, t0, s0
        0x02000001, 0x800F0800, 0x80E40000,               // mov oC0, r0
        0x0000FFFF,                                       // end
      } },
      // Non-prefix write masks (.yzw / .w): the dx9mt miscompile class.
      { "ps_2_0-writemask-yzw", {
        0xFFFF0200,                                       // ps_2_0
        0x05000051, 0xA00F0000,                           // def c0, .25,.5,.75,1
          0x3E800000, 0x3F000000, 0x3F400000, 0x3F800000,
        0x03000002, 0x800E0000, 0x80E40000, 0xA0E40000,   // add r0.yzw, r0, c0
        0x02000001, 0x80080000, 0xA0E40000,               // mov r0.w, c0
        0x02000001, 0x800F0800, 0x80E40000,               // mov oC0, r0
        0x0000FFFF,                                       // end
      } },
      // vs_3_0 sincos (4-operand SM3 form; dxso's operand table expects it)
      // with a .xz partial destination mask and a declared SM3 output.
      { "vs_3_0-sincos", {
        0xFFFE0300,                                       // vs_3_0
        0x0200001F, 0x80000000, 0x900F0000,               // dcl_position v0
        0x0200001F, 0x80000000, 0xE00F0000,               // dcl_position o0
        0x05000051, 0xA00F0001,                           // def c1, ...
          0xBF800000, 0x3F800000, 0xBF800000, 0x3F800000,
        0x05000051, 0xA00F0002,                           // def c2, ...
          0xBF800000, 0x3F800000, 0xBF800000, 0x3F800000,
        0x05000015, 0x80050001, 0x90E40000,               // sincos r1.xz,
          0xA0E40001, 0xA0E40002,                         //   v0, c1, c2
        0x02000001, 0xE00F0000, 0x80E40001,               // mov o0, r1
        0x0000FFFF,                                       // end
      } },
      // SM3: explicit-gradient sample (texldd).
      { "ps_3_0-texldd", {
        0xFFFF0300,                                       // ps_3_0
        0x0200001F, 0x80000005, 0x900F0000,               // dcl_texcoord0 v0
        0x0200001F, 0x90000000, 0xA00F0800,               // dcl_2d s0
        0x0500005D, 0x800F0000, 0x90E40000, 0xA0E40800,   // texldd r0, v0, s0,
          0x90E40000, 0x90E40000,                         //   v0, v0
        0x02000001, 0x800F0800, 0x80E40000,               // mov oC0, r0
        0x0000FFFF,                                       // end
      } },
    };
    return list;
  }

  void runSelftest() {
    std::printf("dxso-corpus: selftest — %zu fixture shader(s)\n", fixtures().size());
    for (const auto& fixture : fixtures()) {
      const bool ok = compileBlob(fixture.code.data(), fixture.code.size(),
                                  std::string("selftest:") + fixture.name);
      std::printf("  %-24s %s\n", fixture.name, ok ? "PASS" : "FAIL");
    }
  }

  void printSummary(const std::string& reportPath) {
    std::printf("\ndxso-corpus: summary\n");
    std::printf("  shaders compiled: %llu\n", (unsigned long long)g_stats.bounded);
    std::printf("  passed:           %llu\n", (unsigned long long)g_stats.passed);
    std::printf("  failed:           %llu\n", (unsigned long long)g_stats.failed);
    std::printf("  scan noise:       %llu (unbounded version tokens; not compiled)\n",
                (unsigned long long)g_stats.unbounded);

    if (!g_stats.failureHistogram.empty()) {
      std::printf("\n  failure histogram (top):\n");
      std::vector<std::pair<uint64_t, const std::string*>> rows;
      for (const auto& [sig, count] : g_stats.failureHistogram)
        rows.push_back({ count, &sig });
      std::sort(rows.begin(), rows.end(),
        [](auto& a, auto& b) { return a.first > b.first; });
      for (size_t i = 0; i < rows.size() && i < 15; i++)
        std::printf("    %6llu  %s\n",
                    (unsigned long long)rows[i].first, rows[i].second->c_str());
    }

    if (!reportPath.empty()) {
      std::ofstream report(reportPath);
      report << "bounded,passed,failed,unbounded\n"
             << g_stats.bounded << ',' << g_stats.passed << ','
             << g_stats.failed << ',' << g_stats.unbounded << '\n';
      for (const auto& [sig, count] : g_stats.failureHistogram)
        report << count << ",\"" << sig << "\"\n";
      std::printf("\ndxso-corpus: report written to %s\n", reportPath.c_str());
    }
  }

  void usage(const char* argv0) {
    std::fprintf(stderr,
      "Usage: %s [--selftest] [--sdp PATH] [--dump PATH]... [--report FILE]\n"
      "  --selftest   run built-in fixture shaders (no game data needed)\n"
      "  --sdp PATH   scan .sdp shader packages (game Data/Shaders dir or file)\n"
      "  --dump PATH  scan DXVK_SHADER_DUMP_PATH dumps (*.sm3_dxbc dir or file)\n"
      "  --report F   write failure histogram to F (CSV)\n",
      argv0);
  }

} // namespace

int main(int argc, char** argv) {
  std::vector<fs::path> sdpPaths;
  std::vector<fs::path> dumpPaths;
  std::string reportPath;
  bool selftest = false;

  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];
    if (arg == "--selftest") {
      selftest = true;
    } else if (arg == "--sdp" && i + 1 < argc) {
      sdpPaths.push_back(argv[++i]);
    } else if (arg == "--dump" && i + 1 < argc) {
      dumpPaths.push_back(argv[++i]);
    } else if (arg == "--report" && i + 1 < argc) {
      reportPath = argv[++i];
    } else if (arg == "-h" || arg == "--help") {
      usage(argv[0]);
      return 0;
    } else {
      std::fprintf(stderr, "dxso-corpus: unknown or incomplete option: %s\n", arg.c_str());
      usage(argv[0]);
      return 2;
    }
  }

  if (!selftest && sdpPaths.empty() && dumpPaths.empty()) {
    usage(argv[0]);
    return 2;
  }

  if (selftest)
    runSelftest();

  for (const auto& path : sdpPaths) {
    if (!fs::exists(path)) {
      std::fprintf(stderr, "dxso-corpus: path not found: %s\n", path.string().c_str());
      return 2;
    }
    scanSdpPath(path);
  }

  for (const auto& path : dumpPaths) {
    if (!fs::exists(path)) {
      std::fprintf(stderr, "dxso-corpus: path not found: %s\n", path.string().c_str());
      return 2;
    }
    scanDumpPath(path);
  }

  printSummary(reportPath);

  if (g_stats.bounded == 0) {
    std::fprintf(stderr, "dxso-corpus: nothing compiled\n");
    return 2;
  }

  return g_stats.failed == 0 ? 0 : 1;
}
