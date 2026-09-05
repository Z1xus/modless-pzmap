#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <sys/stat.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

static std::string getenv_str(const char* k) {
  const char* v = std::getenv(k);
  return v ? std::string(v) : std::string();
}

static bool is_dir(const std::string& p) {
  struct stat st;
  return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool is_file(const std::string& p) {
  struct stat st;
  return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static void mkdirs(const std::string& p) {
  std::string cur;
  for (size_t i = 0; i < p.size(); i++) {
    cur += p[i];
    if (p[i] == '/' || i + 1 == p.size()) {
      if (!cur.empty() && !is_dir(cur)) {
#ifdef _WIN32
        ::mkdir(cur.c_str());
#else
        ::mkdir(cur.c_str(), 0755);
#endif
      }
    }
  }
}

static std::string zomboid_dir() {
  std::string e = getenv_str("ZOMBOID_HOME");
  if (!e.empty()) return e;
  std::string up = getenv_str("USERPROFILE");
  if (!up.empty()) return up + "/Zomboid";
  std::string home = getenv_str("HOME");
  std::string flat = home + "/.var/app/com.valvesoftware.Steam/Zomboid";
  if (is_dir(flat)) return flat;
  return home + "/Zomboid";
}

static std::string exe_dir(const char* argv0) {
#ifdef _WIN32
  char buf[4096];
  DWORD n = GetModuleFileNameA(NULL, buf, sizeof(buf));
  std::string p = n ? std::string(buf, n) : std::string(argv0);
#else
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  std::string p = n > 0 ? std::string(buf, static_cast<size_t>(n)) : std::string(argv0);
#endif
  size_t s = p.find_last_of("/\\");
  return s == std::string::npos ? std::string(".") : p.substr(0, s);
}

static std::string beside(const std::string& dir, const char* name) {
  struct stat st;
  std::string p = dir + "/" + name;
  if (::stat(p.c_str(), &st) == 0) return p;
  if (::stat(name, &st) == 0) return name;
  std::string env = getenv_str("PZ_DIST");
  if (!env.empty()) {
    p = env + "/" + name;
    if (::stat(p.c_str(), &st) == 0) return p;
  }
  p = "/zip/" + std::string(name);
  if (::stat(p.c_str(), &st) == 0) return p;
  return "";
}

static bool copy_file(const std::string& from, const std::string& to) {
  FILE* a = fopen(from.c_str(), "rb");
  if (!a) return false;
  FILE* b = fopen(to.c_str(), "wb");
  if (!b) { fclose(a); return false; }
  char buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), a)) > 0)
    if (fwrite(buf, 1, n, b) != n) { fclose(a); fclose(b); return false; }
  fclose(a);
  fclose(b);
  return true;
}

static bool write_all(const std::string& path, const std::string& text) {
  std::string temp = path + ".pzm-live.tmp";
  FILE* f = fopen(temp.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
  if (fclose(f) != 0) ok = false;
  if (!ok) {
    std::remove(temp.c_str());
    return false;
  }
#ifdef _WIN32
  if (MoveFileExA(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
#else
  if (std::rename(temp.c_str(), path.c_str()) == 0) return true;
#endif
  std::remove(temp.c_str());
  return false;
}

static std::string read_all(const std::string& path) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return "";
  std::string t;
  char buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) t.append(buf, n);
  fclose(f);
  return t;
}

static size_t string_end(const std::string& text, size_t start) {
  bool escaped = false;
  for (size_t i = start + 1; i < text.size(); i++) {
    if (!escaped && text[i] == '"') return i;
    if (!escaped && text[i] == '\\') escaped = true;
    else escaped = false;
  }
  return std::string::npos;
}

static bool vm_args(const std::string& text, size_t& start, size_t& end) {
  for (size_t i = 0; i < text.size();) {
    if (text[i] != '"') {
      i++;
      continue;
    }
    size_t close = string_end(text, i);
    if (close == std::string::npos) return false;
    if (text.compare(i + 1, close - i - 1, "vmArgs") != 0) {
      i = close + 1;
      continue;
    }
    size_t p = close + 1;
    while (p < text.size() && std::isspace(static_cast<unsigned char>(text[p]))) p++;
    if (p >= text.size() || text[p++] != ':') {
      i = close + 1;
      continue;
    }
    while (p < text.size() && std::isspace(static_cast<unsigned char>(text[p]))) p++;
    if (p >= text.size() || text[p] != '[') {
      i = close + 1;
      continue;
    }
    start = p;
    int depth = 1;
    for (p++; p < text.size(); p++) {
      if (text[p] == '"') {
        p = string_end(text, p);
        if (p == std::string::npos) return false;
      } else if (text[p] == '[') {
        depth++;
      } else if (text[p] == ']' && --depth == 0) {
        end = p;
        return true;
      }
    }
    return false;
  }
  return false;
}

static std::string json_string(const std::string& value) {
  static const char hex[] = "0123456789abcdef";
  std::string out = "\"";
  for (char raw : value) {
    unsigned char c = static_cast<unsigned char>(raw);
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 0x20) {
      out += "\\u00";
      out += hex[c >> 4];
      out += hex[c & 15];
    } else {
      out += static_cast<char>(c);
    }
  }
  return out + "\"";
}

static bool update_agent_arg(const std::string& path, const std::string& flag, bool install) {
  std::string text = read_all(path);
  const std::string original = text;
  size_t array_start, array_end;
  if (text.empty() || !vm_args(text, array_start, array_end)) return false;
  std::string backup = path + ".pzm-live.bak";
  if (!is_file(backup) && !write_all(backup, original)) return false;

  const std::string prefix = "-javaagent:pzm-agent.jar";
  size_t hit_start = std::string::npos;
  size_t hit_end = std::string::npos;
  for (size_t p = array_start + 1; p < array_end;) {
    if (text[p] != '"') {
      p++;
      continue;
    }
    size_t close = string_end(text, p);
    if (close == std::string::npos || close > array_end) return false;
    size_t length = close - p - 1;
    if (length >= prefix.size() && text.compare(p + 1, prefix.size(), prefix) == 0 &&
        (length == prefix.size() || text[p + 1 + prefix.size()] == '=')) {
      hit_start = p;
      hit_end = close;
      break;
    }
    p = close + 1;
  }

  if (install) {
    std::string value = json_string(flag);
    if (hit_start != std::string::npos) {
      if (text.compare(hit_start, hit_end - hit_start + 1, value) == 0) return true;
      text.replace(hit_start, hit_end - hit_start + 1, value);
    } else {
      size_t first = array_start + 1;
      while (first < array_end && std::isspace(static_cast<unsigned char>(text[first]))) first++;
      text.insert(array_start + 1, value + (first == array_end ? "" : ","));
    }
  } else {
    if (hit_start == std::string::npos) return true;
    size_t after = hit_end + 1;
    while (after < array_end && std::isspace(static_cast<unsigned char>(text[after]))) after++;
    if (after < array_end && text[after] == ',') {
      text.erase(hit_start, after - hit_start + 1);
    } else {
      size_t before = hit_start;
      while (before > array_start + 1 && std::isspace(static_cast<unsigned char>(text[before - 1]))) before--;
      if (before > array_start + 1 && text[before - 1] == ',') before--;
      text.erase(before, hit_end - before + 1);
    }
  }

  return write_all(path, text);
}

static bool has_game(const std::string& lib) {
  return is_file(lib + "/steamapps/common/ProjectZomboid/ProjectZomboid64.json");
}

static void add_root(std::string& roots, const std::string& root) {
  if (!root.empty()) roots += root + "\n";
}

static std::string vdf_path(const std::string& value) {
  std::string path;
  for (size_t i = 0; i < value.size(); i++) {
    if (value[i] == '\\' && i + 1 < value.size() &&
        (value[i + 1] == '\\' || value[i + 1] == '"')) i++;
    path += value[i];
  }
  return path;
}

static std::string find_game_dir() {
  std::string e = getenv_str("PZ_GAME_DIR");
  if (!e.empty() && is_file(e + "/ProjectZomboid64.json")) return e;
  std::string roots;
  std::string home = getenv_str("HOME");
  std::string pf = getenv_str("ProgramFiles");
  std::string pfx = getenv_str("ProgramFiles(x86)");
  add_root(roots, getenv_str("STEAM_DIR"));
  add_root(roots, pfx.empty() ? "" : pfx + "/Steam");
  add_root(roots, pf.empty() ? "" : pf + "/Steam");
  add_root(roots, home.empty() ? "" : home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam");
  add_root(roots, home.empty() ? "" : home + "/.steam/steam");
  add_root(roots, home.empty() ? "" : home + "/.local/share/Steam");
  add_root(roots, home.empty() ? "" : home + "/Library/Application Support/Steam");
  size_t pos = 0;
  while (pos < roots.size()) {
    size_t nl = roots.find('\n', pos);
    std::string lib = roots.substr(pos, nl - pos);
    pos = nl + 1;
    if (has_game(lib)) return lib + "/steamapps/common/ProjectZomboid";
    std::string vf = read_all(lib + "/steamapps/libraryfolders.vdf");
    size_t q = 0;
    while ((q = vf.find("\"path\"", q)) != std::string::npos) {
      size_t a = vf.find('"', q + 6);
      size_t b = vf.find('"', a + 1);
      if (a == std::string::npos || b == std::string::npos) break;
      std::string extra = vdf_path(vf.substr(a + 1, b - a - 1));
      q = b + 1;
      if (has_game(extra)) return extra + "/steamapps/common/ProjectZomboid";
    }
  }
  return "";
}

static int do_setup(const std::string& dir, const std::string& base) {
  std::string game = find_game_dir();
  if (game.empty()) { std::printf("game not found, set PZ_GAME_DIR\n"); return 3; }
  std::printf("game found: %s\n", game.c_str());
  std::string agent = beside(dir, "pzm-agent.jar");
  if (agent.empty()) { std::printf("pzm-agent.jar not found\n"); return 3; }
  if (!copy_file(agent, game + "/pzm-agent.jar")) { std::printf("agent install failed\n"); return 3; }
  std::printf("agent copied\n");
#ifndef _WIN32
  if (is_file(game + "/jre64/bin/java.dll") && is_file(game + "/ProjectZomboid64.exe")) {
    std::string state_path = game + "/.pzm-live-files";
    std::string state = read_all(state_path);
    bool made_java = !is_file(game + "/java.dll");
    bool made_jli = !is_file(game + "/jli.dll");
    if ((made_java && !copy_file(game + "/jre64/bin/java.dll", game + "/java.dll")) ||
        (made_jli && !copy_file(game + "/jre64/bin/jli.dll", game + "/jli.dll"))) {
      if (made_java) std::remove((game + "/java.dll").c_str());
      if (made_jli) std::remove((game + "/jli.dll").c_str());
      std::printf("engine file copy failed\n");
      return 3;
    }
    if (made_java && state.find("java.dll\n") == std::string::npos) state += "java.dll\n";
    if (made_jli && state.find("jli.dll\n") == std::string::npos) state += "jli.dll\n";
    if ((made_java || made_jli) && !write_all(state_path, state)) {
      if (made_java) std::remove((game + "/java.dll").c_str());
      if (made_jli) std::remove((game + "/jli.dll").c_str());
      std::printf("install state write failed\n");
      return 3;
    }
    if (made_java || made_jli) std::printf("engine compatibility files copied\n");
  }
#endif
  bool had_backup = is_file(game + "/ProjectZomboid64.json.pzm-live.bak");
  if (!update_agent_arg(game + "/ProjectZomboid64.json", "-javaagent:pzm-agent.jar=" + base, true)) {
    std::printf("game settings update failed\n");
    return 3;
  }
  std::printf("settings backup %s\n", had_backup ? "kept" : "created");
  std::printf("settings updated\n");
  return 0;
}

static int do_uninstall(const std::string& base) {
  std::string game = find_game_dir();
  if (game.empty()) { std::printf("game not found, set PZ_GAME_DIR\n"); return 3; }
  std::printf("game: %s\n", game.c_str());
  if (!update_agent_arg(game + "/ProjectZomboid64.json", "", false)) {
    std::printf("settings edit failed\n");
    return 3;
  }
  bool ok = true;
  if (is_file(game + "/pzm-agent.jar") && std::remove((game + "/pzm-agent.jar").c_str()) != 0) ok = false;
  std::string state_path = game + "/.pzm-live-files";
  std::string state = read_all(state_path);
  if (state.find("java.dll\n") != std::string::npos && std::remove((game + "/java.dll").c_str()) != 0 &&
      is_file(game + "/java.dll")) ok = false;
  if (state.find("jli.dll\n") != std::string::npos && std::remove((game + "/jli.dll").c_str()) != 0 &&
      is_file(game + "/jli.dll")) ok = false;
  if (is_file(state_path) && std::remove(state_path.c_str()) != 0) ok = false;
  std::remove((base + "/Lua/pzm_live/live.txt").c_str());
  std::remove((base + "/Lua/pzm_live/live.txt.tmp").c_str());
  if (!ok) { std::printf("helper removal failed\n"); return 3; }
  std::printf("uninstalled. settings backup kept at %s\n",
      (game + "/ProjectZomboid64.json.pzm-live.bak").c_str());
  return 0;
}

static void open_url(const std::string& url) {
#ifdef _WIN32
  std::string cmd = "rundll32 url.dll,FileProtocolHandler " + url;
  std::system(cmd.c_str());
#else
  std::string cmd = "xdg-open '" + url + "' >/dev/null 2>&1 &";
#endif
  std::system(cmd.c_str());
}

static int finish(int result) {
  if (isatty(fileno(stdin))) {
    std::printf("\npress enter to close...");
    std::fflush(stdout);
    while (std::getchar() != '\n' && !std::feof(stdin)) { }
  }
  return result;
}

int main(int argc, char** argv) {
  std::string base = zomboid_dir();

  if (argc >= 2 && std::strcmp(argv[1], "--uninstall") == 0) return finish(do_uninstall(base));

  mkdirs(base + "/Lua/pzm_live");

  if (argc >= 2 && std::strcmp(argv[1], "--open") == 0) {
    std::string frag = argc >= 3 ? argv[2] : "10000x10000x200";
    open_url("https://projectzomboidmap.com/#" + frag);
    return finish(0);
  }
  if (argc >= 2) {
    std::printf("unknown argument: %s\n", argv[1]);
    return finish(2);
  }
  return finish(do_setup(exe_dir(argv[0]), base));
}
