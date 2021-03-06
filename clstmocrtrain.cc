#include "pstring.h"
#include "clstm.h"
#include "clstmhl.h"
#include <assert.h>
#include <iostream>
#include <vector>
#include <memory>
#include <math.h>
#include <Eigen/Dense>
#include <sstream>
#include <fstream>
#include <iostream>
#include <set>
#include <regex>

#include "multidim.h"
#include "pymulti.h"
#include "extras.h"

using namespace Eigen;
using namespace ocropus;
using namespace pymulti;
using std::vector;
using std::map;
using std::make_pair;
using std::shared_ptr;
using std::unique_ptr;
using std::cout;
using std::ifstream;
using std::set;
using std::to_string;
using std_string = std::string;
using std_wstring = std::wstring;
using std::regex;
using std::regex_replace;
#define string std_string
#define wstring std_wstring

string basename(string s) {
    int start = 0;
    for (;; ) {
        auto pos = s.find("/", start);
        if (pos == string::npos) break;
        start = pos+1;
    }
    auto pos = s.find(".", start);
    if (pos == string::npos) return s;
    else return s.substr(0, pos);
}

string read_text(string fname, int maxsize=65536) {
    char buf[maxsize];
    buf[maxsize-1] = 0;
    ifstream stream(fname);
    int n = stream.readsome(buf, maxsize-1);
    while (n > 0 && buf[n-1] == '\n') n--;
    return string(buf, n);
}

void get_codec(vector<int> &codec, const vector<string> &fnames) {
    set<int> codes;
    codes.insert(0);
    for (int i = 0; i < fnames.size(); i++) {
        string fname = fnames[i];
        string base = basename(fname);
        string text = read_text(base+".gt.txt");
        // print(base,":",text);
        wstring text32 = utf8_to_utf32(text);
        for (auto c : text32) codes.insert(int(c));
    }
    codec.clear();
    for (auto c : codes) codec.push_back(c);
    for (int i = 1; i < codec.size(); i++) assert(codec[i] > codec[i-1]);
}

void show(PyServer &py, Sequence &s, int subplot=0) {
    mdarray<float> temp;
    assign(temp, s);
    if (subplot > 0) py.evalf("subplot(%d)", subplot);
    py.imshowT(temp, "cmap=cm.hot");
}

void read_lines(vector<string> &lines, string fname) {
    ifstream stream(fname);
    string line;
    lines.clear();
    while (getline(stream, line)) {
        lines.push_back(line);
    }
}

int main(int argc, char **argv) {
    srandomize();

    if (argc < 2 || argc > 3) THROW("... training [testing]");
    vector<string> fnames, test_fnames;
    read_lines(fnames, argv[1]);
    if (argc > 2) read_lines(test_fnames, argv[2]);
    print("got", fnames.size(), "files,", test_fnames.size(), "tests");

    vector<int> codec;
    get_codec(codec, fnames);
    print("got", codec.size(), "classes");

    CLSTMOCR clstm;
    clstm.target_height = int(getrenv("target_height", 48));
    clstm.createBidi(codec, getienv("nhidden", 100));
    clstm.setLearningRate(getdenv("rate", 1e-4), getdenv("momentum", 0.9));
    clstm.net->info("");

    int maxtrain = getienv("maxtrain", 10000000);
    int save_every = getienv("save_every", 10000);
    string save_name = getsenv("save_name", "_ocr");
    int report_every = getienv("report_every", 100);
    int display_every = getienv("display_every", 0);
    int test_every = getienv("test_every", 10000);
    double test_error = 9999.0;

    PyServer py;
    if (display_every > 0) py.open();
    for (int trial = 0; trial < maxtrain; trial++) {
        if (trial > 0 && test_fnames.size() > 0 && test_every > 0 && trial%test_every == 0) {
            double errors = 0.0;
            double count = 0.0;
            for (int test = 0; test < test_fnames.size(); test++) {
                string fname = test_fnames[test];
                string base = basename(fname);
                string gt = read_text(base+".gt.txt");
                mdarray<float> raw;
                read_png(raw, fname.c_str(), true);
                for (int i = 0; i < raw.size(); i++) raw[i] = 1-raw[i];
                wstring pred = clstm.predict(raw);
                count += gt.size();
                errors += levenshtein(pred, gt);
            }
            test_error = errors/count;
            print("ERROR", trial, test_error, "   ", errors, count);
        }
        int sample = irandom() % fnames.size();
        if (trial > 0 && save_every > 0&&trial%save_every == 0) {
            string fname = save_name+"-"+to_string(trial)+".buf";
            clstm.save(fname);
        }
        string fname = fnames[sample];
        string base = basename(fname);
        string gt = read_text(base+".gt.txt");
        mdarray<float> raw;
        read_png(raw, fname.c_str(), true);
        for (int i = 0; i < raw.size(); i++) raw[i] = 1-raw[i];
        string pred = clstm.train_utf8(raw, gt);
        if (trial%display_every == 0) {
            py.evalf("clf");
            show(py, clstm.net->inputs, 411);
            show(py, clstm.net->outputs, 412);
            show(py, clstm.targets, 413);
            show(py, clstm.aligned, 414);
        }
        if (trial%report_every == 0) {
            mdarray<float> temp;
            print(trial);
            print("TRU", gt);
            print("ALN", clstm.aligned_utf8());
            print("OUT", pred);
        }
    }

    return 0;
}
