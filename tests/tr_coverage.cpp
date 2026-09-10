// tr_coverage.cpp — Translation audit for the GUI.
//
// Static check that every user-visible English string threaded through the
// translation helpers (tr / trf / trlbl / trmlbl / trbtn / trchk / trtip /
// tr_combo_fill) has a Turkish entry in gui/tr.inl, and vice versa.
//
// Directories are given as command-line arguments; gui/tr.inl is always
// scanned.  Exit code:
//   0 = full coverage (no missing translations)
//   1 = at least one missing translation (UI would show English in Türkçe)
// Orphans (dict entries never referenced) are reported as warnings only.
//
// String-literal handling: C++ adjacent-literal concatenation ("a" "b") is
// joined with no separator; escapes are copied verbatim so dict keys and code
// literals compare equal without needing a full escape decoder.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

static std::string slurp(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::string s;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

// At pos (start of a '"'), parse a sequence of adjacent C string literals and
// return the concatenated raw interiors.  On success advances *pos past the
// last closing quote and returns true.
static bool parse_string_seq(const std::string& s, size_t* pos, std::string* out) {
    size_t i = *pos;
    while (true) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
            i++;
        if (i >= s.size() || s[i] != '"') break;
        i++; // opening quote
        while (i < s.size()) {
            char c = s[i];
            if (c == '\\') {
                // copy backslash + next char verbatim (handles \" \\ \n)
                out->push_back(c);
                if (i + 1 < s.size()) out->push_back(s[i + 1]);
                i += 2;
                continue;
            }
            if (c == '"') { i++; break; } // closing quote
            out->push_back(c);
            i++;
        }
    }
    if (out->empty()) return false; // failed parse: leave *pos untouched
    *pos = i;
    return true;
}

// Advance *i over the FIRST argument of trtip(widget, key) so we can read the
// second argument (the translatable key).  Handles balanced ( ) [ ] { } and
// skips quoted strings inside the widget expression.
static void skip_first_arg(const std::string& s, size_t* i) {
    int depth = 0;
    while (*i < s.size()) {
        char c = s[*i];
        if (c == '"') {
            (*i)++;
            while (*i < s.size() && s[*i] != '"') {
                if (s[*i] == '\\') (*i)++;
                (*i)++;
            }
            if (*i < s.size()) (*i)++;
            continue;
        }
        if (c == '(' || c == '[' || c == '{') depth++;
        else if (c == ')' || c == ']' || c == '}') depth--;
        else if (c == ',' && depth == 0) return;
        (*i)++;
    }
}

static bool is_c_ident(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           (c >= '0' && c <= '9');
}

static std::string line_of(const std::string& src, size_t off) {
    size_t ln = 1;
    for (size_t i = 0; i < off && i < src.size(); i++)
        if (src[i] == '\n') ln++;
    return std::to_string(ln);
}

// true iff src[off] lies inside a `//` line comment or `/* */` block comment.
// Prevents comment prose like "// tr(some_var)" from entering the dynamic list.
static bool in_comment(const std::string& src, size_t off) {
    size_t i = 0;
    while (i < src.size() && i < off) {
        char c = src[i];
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            while (i < src.size() && src[i] != '\n') {
                if (i >= off) return true;
                i++;
            }
            continue;
        }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
            i += 2;
            bool closed = false;
            while (i + 1 < src.size()) {
                if (src[i] == '*' && src[i + 1] == '/') { i += 2; closed = true; break; }
                if (i >= off) return true;   // off still inside the block
                i++;
            }
            if (!closed) return true;
            continue;
        }
        i++;
    }
    return false;
}

// P114 BUG-G: braced combo-array definitions are recognized in every spelling.
// Accepted forms (after the array name):
//   NAME[] = { / NAME[5] = { / NAME = { / NAME{...} / Type NAME[] = { ...
//   std::array<...> NAME = { ...
// A ';' or '(' ')' before '{' means call/declaration — not a definition.
static bool combo_array_open(const std::string& src, size_t q, size_t* openOut) {
    size_t i = q;
    size_t lim = std::min(src.size(), q + 160);
    while (i < lim) {
        char c = src[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { i++; continue; }
        if (c == '[') {
            i++;
            while (i < lim && src[i] != ']') {
                if (src[i] == ';' || src[i] == '(' || src[i] == ')') return false;
                i++;
            }
            if (i >= lim) return false;
            i++;
            continue;
        }
        if (c == '<') {   // std::array<...>
            int d = 0;
            while (i < lim) {
                if (src[i] == '<') d++;
                else if (src[i] == '>') { d--; if (d == 0) { i++; break; } }
                else if (src[i] == ';' || src[i] == '(' || src[i] == ')') return false;
                i++;
            }
            continue;
        }
        if (c == '=') {
            i++;
            while (i < lim && (src[i] == ' ' || src[i] == '\t' || src[i] == '\n' || src[i] == '\r')) i++;
            if (i < lim && src[i] == '{') { *openOut = i; return true; }
            return false;   // '=' gevolgd door geen '{' → geen array-init
        }
        if (c == '{') { *openOut = i; return true; }  // NAME{...} / NAME[]{...}
        if (c == ';' || c == '(' || c == ')') return false;
        return false;
    }
    return false;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: tr_coverage gui_source...  (gui/tr.inl always scanned)\n";
        return 2;
    }

    std::string tr_path;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]).find("tr.inl") != std::string::npos) tr_path = argv[i];
    }
    if (tr_path.empty()) {
        // fall back to <dir of first source>/tr.inl
        std::string d = argv[1];
        size_t slash = d.find_last_of('/');
        tr_path = (slash == std::string::npos ? "" : d.substr(0, slash + 1)) + "tr.inl";
    }

    std::set<std::string> used;   // EN strings passed to translation helpers
    std::set<std::string> orphans;
    size_t dynamic_calls = 0;       // P114 BUG-F: değişken anahtarlı çağrılar
    size_t empty_str_calls = 0;     // P114 BUG-F: boş-string anahtarlı çağrılar
    std::vector<std::pair<std::string, size_t>> dyn_sites; // (file, offset)
    std::vector<std::string> combo_def_missing;            // BUG-G: tanımı bulunamayan
    bool dict_has_empty = false;

    const std::set<std::string> call_names = {
        "tr", "trf", "trlbl", "trmlbl", "trbtn", "trchk", "trtip",
    };

    // Array names populated via tr_combo_fill(x, ARRAY).
    std::set<std::string> combo_arrays;
    std::map<std::string, std::string> file_of; // for later array scanning

    for (int i = 1; i < argc; i++) {
        std::string path = argv[i];
        std::string src = slurp(path);
        file_of[path] = src;

        size_t i2 = 0;
        while (i2 < src.size()) {
            // skip non-identifier
            char c = src[i2];
            if (is_c_ident(c)) {
                size_t start = i2;
                while (i2 < src.size() && is_c_ident(src[i2])) i2++;
                std::string ident = src.substr(start, i2 - start);
                size_t skip = 0; // # leading args before the translatable string
                if (ident == "trtip") skip = 1;        // trtip(widget, key)
                else if (ident == "grid_row" || ident == "grid_row2")
                    skip = 2;                          // grid_row(g, row, label, w)
                if (call_names.count(ident) || skip > 0) {
                    // expect '('
                    size_t j = i2;
                    while (j < src.size() && src[j] == ' ') j++;
                    if (j < src.size() && src[j] == '(') {
                        size_t arg = j + 1;
                        for (size_t k = 0; k < skip; k++) {
                            skip_first_arg(src, &arg);
                            if (arg < src.size() && src[arg] == ',') arg++;
                        }
                        std::string key;
                        if (parse_string_seq(src, &arg, &key)) {
                            if (std::getenv("TRC_DEBUG"))
                                std::fprintf(stderr, "DBG call %s @%zu in %s: %s\n", ident.c_str(), start, path.c_str(), key.c_str());
                            used.insert(key);
                        } else {
                            // P114 BUG-F: boş-string literal ("") ayrı kanal; geri
                            // kalan değişken/ifade anahtarlar site'lenip uyarılır.
                            // Yorumlardaki tr(...) söz öbeği sayılmaz.
                            if (!in_comment(src, start)) {
                                size_t sp = arg;
                                while (sp < src.size() && (src[sp] == ' ' || src[sp] == '\t' ||
                                                           src[sp] == '\n' || src[sp] == '\r')) sp++;
                                if (sp + 1 < src.size() && src[sp] == '"' && src[sp + 1] == '"') {
                                    empty_str_calls++;
                                } else {
                                    dynamic_calls++; // e.g. tr(err.c_str())
                                    dyn_sites.push_back({path, start});
                                }
                            }
                        }
                    }
                } else if (ident == "tr_combo_fill") {
                    size_t j = i2;
                    while (j < src.size() && src[j] == ' ') j++;
                    if (j < src.size() && src[j] == '(') {
                        size_t arg = j + 1;
                        // first arg: widget expr; second arg: array name (or tr())
                        size_t comma = src.find(',', arg);
                        if (comma != std::string::npos) {
                            size_t k = comma + 1;
                            while (k < src.size() && (src[k] == ' ' || src[k] == '\n' || src[k] == '\t')) k++;
                            // If second arg is an identifier (array), grab it
                            char kc = src[k];
                            if (is_c_ident(kc)) {
                                size_t kstart = k;
                                while (k < src.size() && is_c_ident(src[k])) k++;
                                combo_arrays.insert(src.substr(kstart, k - kstart));
                            }
                        }
                    }
                }
            } else {
                i2++;
            }
        }
    }

    // Extract literals from tr_combo_fill arrays (e.g. MODE_KEYS).  Match the
// array *definition* so unrelated '{' in a file (function bodies, dict
// entries) is never picked up. P114 BUG-G: NAME[...]={} yanında NAME={},
// NAME{...} ve std::array<...> NAME={} biçimleri de taranır.
const std::set<std::string> cpp_keywords = {
    "const", "static", "nullptr", "true", "false", "int", "char", "auto",
    "void", "bool", "double", "float", "return", "if", "else", "for",
    "while", "sizeof",
};
for (const auto& an : combo_arrays) {
    if (cpp_keywords.count(an)) continue;
    bool found_any = false;
    for (const auto& fp : file_of) {
        const std::string& src = fp.second;
        size_t p = 0;
        while ((p = src.find(an, p)) != std::string::npos) {
            size_t open = 0;
            if (combo_array_open(src, p + an.size(), &open)) {
                found_any = true;
                if (std::getenv("TRC_DEBUG"))
                    std::fprintf(stderr, "DBG combo %s @%zu in %s\n", an.c_str(), open, fp.first.c_str());
                size_t cur = open + 1;
                while (cur < src.size() && src[cur] != '}') {
                    std::string lit;
                    if (parse_string_seq(src, &cur, &lit)) {
                        if (std::getenv("TRC_DEBUG"))
                            std::fprintf(stderr, "DBG combo-item %s in %s: %s\n", an.c_str(), fp.first.c_str(), lit.c_str());
                        used.insert(lit);
                    }
                    else cur++;
                }
            }
            p += an.size();
        }
    }
    if (!found_any) {
        // P114 BUG-G: sessiz atlama olmaz — tanımı bulunamayan combo dizisinin
        // anahtarları doğrulanamaz; insan müdahalesi gerekir (FAIL).
        combo_def_missing.push_back(an);
        std::fprintf(stderr, "UYARI: '%s' combo dizisinin tanımı bulunamadı — "
                             "anahtarları taranamadı\n", an.c_str());
    }
}

    // ── Parse the dictionary from tr.inl ─────────────────────────────────────
    std::string trsrc = slurp(tr_path);
    std::set<std::string> dict_keys;
    {
        size_t block = trsrc.find("D = {");
        if (block == std::string::npos) {
            std::cerr << "tr.inl: 'D = {' block not found\n";
            return 2;
        }
        size_t cur = block + 5;
        size_t close = trsrc.find("};", cur);
        while (cur < close) {
            // skip to '{'
            while (cur < close && trsrc[cur] != '{') cur++;
            if (cur >= close) break;
            cur++; // '{'
            std::string key;
            if (!parse_string_seq(trsrc, &cur, &key)) {
                // P114 BUG-F: boş-string sözlük girdisi {"", ...} ayrı yakalanır.
                size_t e = cur;
                while (e < trsrc.size() && (trsrc[e] == ' ' || trsrc[e] == '\t' ||
                                            trsrc[e] == '\n' || trsrc[e] == '\r')) e++;
                if (e + 1 < trsrc.size() && trsrc[e] == '"' && trsrc[e + 1] == '"')
                    dict_has_empty = true;
                cur++; // non-literal first element — skip to next '{'
                continue;
            }
            dict_keys.insert(key);
            // skip the rest of this entry up to the matching '}'
            size_t rc = trsrc.find('}', cur);
            if (rc == std::string::npos || rc > close) break;
            cur = rc + 1;
        }
    }

    // ── Report ───────────────────────────────────────────────────────────────
    std::vector<std::string> missing, orphan_list;
    for (const auto& u : used)
        if (!dict_keys.count(u)) missing.push_back(u);
    for (const auto& d : dict_keys)
        if (!used.count(d)) orphan_list.push_back(d);

    std::sort(missing.begin(), missing.end());
    std::sort(orphan_list.begin(), orphan_list.end());

    std::cout << "tr.inl dictionary:      " << dict_keys.size() << " entries\n";
    std::cout << "translated call sites:  " << used.size() << " unique strings\n";
    std::cout << "dynamic (skipped) calls:" << dynamic_calls << "\n";
    std::cout << "empty-string calls:     " << empty_str_calls << "\n\n";

    // P114 BUG-F: değişken anahtarlı çağrılar statik doğrulanamaz — kanal artık
    // sessiz SAYI değil, site-site listelenen bir UYARI (insan doğrulaması).
    if (!dyn_sites.empty()) {
        std::cout << "DİNAMİK ANAHTAR UYARISI: " << dyn_sites.size()
                  << " çağrı site'si statik doğrulanamadı — translate edilebilirlik "
                     "insan gözüyle teyit edilmeli:\n";
        for (const auto& ds : dyn_sites) {
            const auto it = file_of.find(ds.first);
            std::string ln = (it != file_of.end()) ? line_of(it->second, ds.second)
                                                    : std::to_string(ds.second);
            std::cout << "  " << ds.first << ":" << ln << "\n";
        }
    }
    if (empty_str_calls > 0 && !dict_has_empty) {
        std::cout << "UYARI: " << empty_str_calls
                  << " çağrı boş-string (\"\") anahtarı kullanıyor ve sözlükte \"\" "
                     "yok (boş→boş render edilir, çeviri gerekmez)\n";
    }

    bool fatal = !missing.empty() || !combo_def_missing.empty();
    if (!missing.empty()) {
        std::cout << "MISSING TRANSLATIONS (" << missing.size() << ") — shown in English:\n";
        for (auto& m : missing) std::cout << "  \"" << m << "\"\n";
        std::cout << "\n";
    } else {
        std::cout << "MISSING: none — every UI string has a Turkish entry\n";
    }

    if (!combo_def_missing.empty()) {
        std::cout << "FAIL: " << combo_def_missing.size()
                  << " combo dizisinin tanımı bulunamadı (ait anahtarlar taranamadı):\n";
        for (auto& c : combo_def_missing) std::cout << "  \"" << c << "\"\n";
    }

    if (!orphan_list.empty()) {
        std::cout << "\nORPHANS (unreferenced entries, " << orphan_list.size() << "):\n";
        for (auto& o : orphan_list) std::cout << "  \"" << o << "\"\n";
    }

    std::cout << (fatal ? "\nResult: FAIL (missing translations)"
                        : "\nResult: PASS");
    if (!combo_def_missing.empty())
        std::cout << " — combo dizisi taraması eksik (BUG-G)";
    if (!missing.empty())
        std::cout << " — eksik çeviri(ler) mevcut";
    std::cout << "\n";
    return fatal ? 1 : 0;
}