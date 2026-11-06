// deviation_probe.cpp — CORR-1 olcum araci (salt-okunur).
// Oracle ref/local cikti + known_deviations.txt'i okur, sinif sinif istatistik doker.
// Derleme: g++ -O2 -std=c++20 olcum/aj4/deviation_probe.cpp -o /tmp/opencode/devprobe
// Kullanim: /tmp/opencode/devprobe ref.out local.out known_deviations.txt
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

static std::string cls_of(const std::string& c) {
    if (c == "classic_gain_exp_le1") return "C1 classic_gain_exp_le1";
    if (c == "power_tinyexp_floor") return "C3 power_tinyexp_floor";
    if (c == "p155_io_cap0_legacy") return "C4 p155_io_cap0_legacy";
    if (c == "p155_io_cap0_gain") return "C5 p155_io_cap0_gain";
    return "C2 power/sync-spd0";
}

int main(int argc, char** argv) {
    if (argc != 4) { std::fprintf(stderr, "kullanim: devprobe ref.out local.out known.txt\n"); return 2; }
    std::map<std::string, double> R, L;
    auto load = [&](const char* p, std::map<std::string, double>& m) {
        std::ifstream f(p);
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            std::string name, spd, gain;
            if (!std::getline(ss, name, '\t')) continue;
            std::getline(ss, spd, '\t');
            std::getline(ss, gain);
            char buf[64];
            std::snprintf(buf, sizeof buf, "%s\t%g", name.c_str(), std::stod(spd));
            m[buf] = std::stod(gain);
        }
    };
    load(argv[1], R);
    load(argv[2], L);
    std::set<std::string> known;
    {
        std::ifstream f(argv[3]);
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line);
            std::string name, spd;
            std::getline(ss, name, '\t');
            std::getline(ss, spd);
            char buf[64];
            std::snprintf(buf, sizeof buf, "%s\t%g", name.c_str(), std::stod(spd));
            known.insert(buf);
        }
    }
    struct Stat { int n = 0, npos = 0, nneg = 0, nnan = 0; double maxabs = 0, maxrel = 0; std::vector<double> rels; double worst_spd = 0; std::string worst_case; double worst_loc = 0, worst_ref = 0; };
    std::map<std::string, Stat> S;
    int genuine = 0;
    for (auto& kv : known) {
        auto itR = R.find(kv), itL = L.find(kv);
        if (itR == R.end() || itL == L.end()) { std::printf("EKSIK SATIR: %s\n", kv.c_str()); continue; }
        double rv = itR->second, lv = itL->second;
        std::string cname = kv.substr(0, kv.find('\t'));
        Stat& s = S[cls_of(cname)];
        s.n++;
        if (!std::isfinite(rv) || !std::isfinite(lv)) {
            s.nnan++;
            if (lv > rv || (std::isfinite(lv) && !std::isfinite(rv))) s.npos++;
            else s.nneg++;
            genuine++;
            continue;
        }
        double ad = std::fabs(lv - rv);
        double den = std::max({std::fabs(rv), std::fabs(lv), 1e-300});
        double rel = ad / den;
        if (rel > 1e-9) genuine++;
        s.maxabs = std::max(s.maxabs, ad);
        if (rel > s.maxrel) {
            s.maxrel = rel;
            s.worst_case = kv;
            s.worst_loc = lv;
            s.worst_ref = rv;
        }
        s.rels.push_back(rel);
        if (lv > rv) s.npos++;
        else if (lv < rv) s.nneg++;
    }
    std::printf("%-24s %3s %12s %12s %14s %4s %4s %4s\n", "sinif", "n", "max_mutlak", "max_goreli", "yon(+/-)", "nan", "p99rel", "x");
    for (auto& kv : S) {
        Stat& s = kv.second;
        std::sort(s.rels.begin(), s.rels.end());
        double p99 = s.rels.empty() ? 0 : s.rels[(s.rels.size() * 99) / 100];
        std::printf("%-24s %3d %12.3e %12.3e %6d/%-6d %4d %.3e\n",
                    kv.first.c_str(), s.n, s.maxabs, s.maxrel, s.npos, s.nneg, s.nnan, p99);
        std::printf("    en-kotu: %s local=%.6g ref=%.6g\n", s.worst_case.c_str(), s.worst_loc, s.worst_ref);
    }
    std::printf("bilinen-satir: %zu, gercekten-sapan(>1e-9 veya finite-farki): %d\n", known.size(), genuine);
    return 0;
}
