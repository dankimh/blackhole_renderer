#include "render/gl/GLShader.h"
#include "util/File.h"
#include "util/Log.h"
#include <set>
#include <sstream>

namespace bh::gl {
namespace fs = std::filesystem;

static void expand(const fs::path& root, const std::string& rel, std::set<std::string>& seen,
                   std::string& out, bool& versionSeen) {
    if (seen.count(rel)) return;
    seen.insert(rel);
    std::string text;
    if (!file::readText(root / rel, text)) {
        LOG_ERROR("Shader not found: %s", (root / rel).string().c_str());
        return;
    }
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::string t = line;
        size_t p = t.find_first_not_of(" \t");
        if (p != std::string::npos) t = t.substr(p);
        if (t.rfind("#include", 0) == 0) {
            size_t a = t.find('"'), b = t.find('"', a + 1);
            if (a != std::string::npos && b != std::string::npos) {
                expand(root, t.substr(a + 1, b - a - 1), seen, out, versionSeen);
                out += "\n";
            }
            continue;
        }
        if (t.rfind("#version", 0) == 0) {
            if (!versionSeen) {
                out += line + "\n#define GL 1\n";
                versionSeen = true;
            }
            continue;
        }
        out += line + "\n";
    }
}

std::string preprocess(const fs::path& root, const std::string& rel) {
    std::set<std::string> seen;
    std::string out;
    bool ver = false;
    expand(root, rel, seen, out, ver);
    if (!ver) out = "#version 460\n#define GL 1\n" + out;
    return out;
}

static GLuint compile(GLenum type, const std::string& src, const std::string& name) {
    GLuint sh = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(sh, 1, &p, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        LOG_ERROR("Shader compile failed (%s):\n%s", name.c_str(), log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

GLuint buildProgram(const fs::path& root, const std::vector<Stage>& stages) {
    GLuint prog = glCreateProgram();
    std::vector<GLuint> shaders;
    for (const auto& st : stages) {
        GLuint sh = compile(st.type, preprocess(root, st.file), st.file);
        if (!sh) { glDeleteProgram(prog); return 0; }
        glAttachShader(prog, sh);
        shaders.push_back(sh);
    }
    glLinkProgram(prog);
    for (GLuint sh : shaders) glDeleteShader(sh);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        LOG_ERROR("Program link failed (%s):\n%s", stages[0].file.c_str(), log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}
}  // namespace bh::gl
