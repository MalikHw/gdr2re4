/**
 * Converts GDR replay format <-> RE4 (tobyadd/GDH) replay format
 * By MalikHw47
 *
 * RE4 binary layout (from tobyadd/GDH src/core/replayEngine.cpp):
 *   [3 bytes]  magic "RE4"
 *   [4 bytes]  float  fps / TPS
 *   [8 bytes]  uint64 physicSize
 *   physicSize x { uint64 frame, float x, float y, double y_accel, uint8
 * isPlayer2 } [8 bytes]  uint64 inputSize inputSize  x { uint64 frame, uint8
 * down, int32 button, uint8 isPlayer2 }
 *
 * GDR format: handled by maxnut/GDReplayFormat (libGDR)
 */
// trigger

#include <gdr/gdr.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct PhysicData {
  uint64_t frame;
  float x;
  float y;
  double y_accel;
  bool isPlayer2;
};

struct InputData {
  uint64_t frame;
  bool down;
  int button; // 1=Jump, 2=Left, 3=Right
  bool isPlayer2;
};

struct RE4Replay {
  float tps = 240.f;
  std::vector<PhysicData> physic;
  std::vector<InputData> inputs;
};

struct MhwBot : gdr::Replay<MhwBot, gdr::Input<>> {
  MhwBot() : Replay("mhwbot", 1) {}
};

bool readRE4(const fs::path &path, RE4Replay &out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::cerr << "Error: cannot open '" << path.string() << "'\n";
    return false;
  }

  char magic[3];
  in.read(magic, 3);
  if (magic[0] != 'R' || magic[1] != 'E' || magic[2] != '4') {
    std::cerr << "Error: not a valid RE4 file (bad magic)\n";
    return false;
  }

  in.read(reinterpret_cast<char *>(&out.tps), sizeof(out.tps));

  uint64_t physicSize = 0;
  in.read(reinterpret_cast<char *>(&physicSize), sizeof(physicSize));
  out.physic.resize(physicSize);

  for (auto &p : out.physic) {
    in.read(reinterpret_cast<char *>(&p.frame), sizeof(p.frame));
    in.read(reinterpret_cast<char *>(&p.x), sizeof(p.x));
    in.read(reinterpret_cast<char *>(&p.y), sizeof(p.y));
    in.read(reinterpret_cast<char *>(&p.y_accel), sizeof(p.y_accel));
    uint8_t p2 = 0;
    in.read(reinterpret_cast<char *>(&p2), 1);
    p.isPlayer2 = (p2 != 0);
  }

  uint64_t inputSize = 0;
  in.read(reinterpret_cast<char *>(&inputSize), sizeof(inputSize));
  out.inputs.resize(inputSize);

  for (auto &i : out.inputs) {
    in.read(reinterpret_cast<char *>(&i.frame), sizeof(i.frame));
    uint8_t down = 0;
    in.read(reinterpret_cast<char *>(&down), 1);
    i.down = (down != 0);
    int32_t btn = 0;
    in.read(reinterpret_cast<char *>(&btn), sizeof(btn));
    i.button = static_cast<int>(btn);
    uint8_t p2 = 0;
    in.read(reinterpret_cast<char *>(&p2), 1);
    i.isPlayer2 = (p2 != 0);
  }

  if (!in) {
    std::cerr << "Error: file ended unexpectedly while reading RE4\n";
    return false;
  }
  return true;
}

bool writeRE4(const fs::path &path, const RE4Replay &r) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    std::cerr << "Error: cannot create '" << path.string() << "'\n";
    return false;
  }

  out.write("RE4", 3);
  out.write(reinterpret_cast<const char *>(&r.tps), sizeof(r.tps));

  uint64_t physicSize = r.physic.size();
  out.write(reinterpret_cast<const char *>(&physicSize), sizeof(physicSize));
  for (const auto &p : r.physic) {
    out.write(reinterpret_cast<const char *>(&p.frame), sizeof(p.frame));
    out.write(reinterpret_cast<const char *>(&p.x), sizeof(p.x));
    out.write(reinterpret_cast<const char *>(&p.y), sizeof(p.y));
    out.write(reinterpret_cast<const char *>(&p.y_accel), sizeof(p.y_accel));
    uint8_t p2 = p.isPlayer2 ? 1 : 0;
    out.write(reinterpret_cast<const char *>(&p2), 1);
  }

  uint64_t inputSize = r.inputs.size();
  out.write(reinterpret_cast<const char *>(&inputSize), sizeof(inputSize));
  for (const auto &i : r.inputs) {
    out.write(reinterpret_cast<const char *>(&i.frame), sizeof(i.frame));
    uint8_t down = i.down ? 1 : 0;
    out.write(reinterpret_cast<const char *>(&down), 1);
    int32_t btn = static_cast<int32_t>(i.button);
    out.write(reinterpret_cast<const char *>(&btn), sizeof(btn));
    uint8_t p2 = i.isPlayer2 ? 1 : 0;
    out.write(reinterpret_cast<const char *>(&p2), 1);
  }

  if (!out) {
    std::cerr << "Error: write failed for '" << path.string() << "'\n";
    return false;
  }
  return true;
}

bool re4ToGdr(const fs::path &inPath, const fs::path &outPath) {
  RE4Replay re4;
  if (!readRE4(inPath, re4))
    return false;

  MhwBot replay;
  replay.framerate = static_cast<double>(re4.tps);

  for (const auto &i : re4.inputs) {
    gdr::Input<> inp;
    inp.frame = i.frame;
    inp.button = static_cast<uint8_t>(i.button);
    inp.player2 = i.isPlayer2;
    inp.down = i.down;
    replay.inputs.push_back(inp);
  }

  if (!replay.inputs.empty()) {
    replay.duration = static_cast<float>(replay.inputs.back().frame) /
                      static_cast<float>(re4.tps);
  }

  auto result = replay.save(outPath.string());
  if (result.isErr()) {
    std::cerr << "Error saving GDR: " << result.unwrapErr() << "\n";
    return false;
  }

  std::cout << "Converted RE4 -> GDR: " << outPath.string() << "\n";
  std::cout << "  TPS/framerate : " << re4.tps << "\n";
  std::cout << "  Inputs        : " << re4.inputs.size() << "\n";
  return true;
}

bool gdrToRe4(const fs::path &inPath, const fs::path &outPath) {
  MhwBot replay;
  auto result = replay.load(inPath.string());
  if (result.isErr()) {
    std::cerr << "Error loading GDR: " << result.unwrapErr() << "\n";
    return false;
  }

  RE4Replay re4;
  re4.tps = static_cast<float>(replay.framerate);

  for (const auto &inp : replay.inputs) {
    InputData id;
    id.frame = inp.frame;
    id.down = inp.down;
    id.button = static_cast<int>(inp.button);
    id.isPlayer2 = inp.player2;
    re4.inputs.push_back(id);
  }

  if (!writeRE4(outPath, re4))
    return false;

  std::cout << "Converted GDR -> RE4: " << outPath.string() << "\n";
  std::cout << "  TPS/framerate : " << re4.tps << "\n";
  std::cout << "  Inputs        : " << re4.inputs.size() << "\n";
  return true;
}

static void printHelp(const char *argv0) {
  std::cout
      << "gdr2re4 and vice versa By MalikHw47\n"
      << "Converts the GDR replay format into RE4 (used by tobyadd/gdh) and "
         "vice versa\n"
      << "\n"
      << "Usage:\n"
      << "  " << argv0
      << " <file.re4|file.gdr|file.gdr2>      (drag & drop / positional)\n"
      << "  " << argv0 << " -i <input> [-o <output>]\n"
      << "\n"
      << "Options:\n"
      << "  -i, --input   <path>   Input file (.re4, .gdr, or .gdr2)\n"
      << "  -o, --output  <path>   Output file (optional; derived from input "
         "if omitted)\n"
      << "  -h, --help             Show this help message\n"
      << "\n"
      << "Extension support:\n"
      << "  .re4        -> converted to .gdr2 (RE4 to GDR)\n"
      << "  .gdr/.gdr2  -> converted to .re4  (GDR to RE4)\n";
}

static std::string toLower(std::string s) {
  for (auto &c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printHelp(argv[0]);
    return 0;
  }

  std::string inputPath;
  std::string outputPath;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-h" || arg == "--help") {
      printHelp(argv[0]);
      return 0;
    } else if ((arg == "-i" || arg == "--input") && i + 1 < argc) {
      inputPath = argv[++i];
    } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
      outputPath = argv[++i];
    } else if (arg[0] != '-') {
      if (inputPath.empty())
        inputPath = arg;
      else if (outputPath.empty())
        outputPath = arg;
    } else {
      std::cerr << "Unknown option: " << arg << "\nTry --help for usage.\n";
      return 1;
    }
  }

  if (inputPath.empty()) {
    std::cerr << "Error: no input file specified. Use --help for usage.\n";
    return 1;
  }

  fs::path inPath(inputPath);
  if (!fs::exists(inPath)) {
    std::cerr << "Error: file not found: " << inputPath << "\n";
    return 1;
  }

  std::string ext = toLower(inPath.extension().string());

  fs::path outPath;
  if (!outputPath.empty()) {
    outPath = fs::path(outputPath);
  } else {
    fs::path exeDir = fs::path(argv[0]).parent_path();
    if (exeDir.empty())
      exeDir = fs::current_path();

    if (ext == ".re4") {
      outPath = exeDir / (inPath.stem().string() + ".gdr2");
    } else {
      outPath = exeDir / (inPath.stem().string() + ".re4");
    }
  }

  if (ext == ".re4") {
    return re4ToGdr(inPath, outPath) ? 0 : 1;
  } else if (ext == ".gdr" || ext == ".gdr2") {
    return gdrToRe4(inPath, outPath) ? 0 : 1;
  } else {
    std::cerr << "Error: unrecognised extension '" << ext
              << "'. Expected .re4, .gdr, or .gdr2\n";
    return 1;
  }
}
