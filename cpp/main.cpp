#include "core.hpp"
#include "dslvm.hpp"
#include "expr.hpp"
#include "memory_budget.hpp"
#include "notices_embedded.hpp"
#include "schema_embedded.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <memory>
#include <string>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
namespace {
using dslvm::Args;
using dslvm::Value;
using lmcell::Matrix;
struct Cell {};
struct Sim {
  lmcell::Config config;
  bool ran = false;
};
struct Type {
  Sim *sim;
  size_t index;
  lmcell::CellType &data() { return sim->config.types.at(index); }
};
struct Host {
  std::vector<std::unique_ptr<Sim>> sims;
  std::vector<std::unique_ptr<Type>> types;
  std::vector<std::unique_ptr<lmcell::Results>> results;
  std::vector<std::unique_ptr<Matrix>> matrices;
  template <class T>
  Value hold(std::vector<std::unique_ptr<T>> &a, const char *t, T v) {
    if (a.size() >= 10000)
      throw dslvm::Error("host object limit exceeded");
    a.push_back(std::make_unique<T>(std::move(v)));
    return Value::object(t, a.back().get());
  }
};
Sim &sim(void *p) { return *static_cast<Sim *>(p); }
Type &type(void *p) { return *static_cast<Type *>(p); }
lmcell::Results &result(void *p) { return *static_cast<lmcell::Results *>(p); }
Matrix &matrix(void *p) { return *static_cast<Matrix *>(p); }
void editable(Sim &s) {
  if (s.ran)
    throw dslvm::Error("simulation already run; builders are frozen");
}
void finite(double x, const char *name) {
  if (!std::isfinite(x))
    throw dslvm::Error(std::string(name) + " must be finite");
}
void rate(double x, const char *name) {
  finite(x, name);
  if (x < 0)
    throw dslvm::Error(std::string(name) + " must be nonnegative");
}
std::string fmt(double x) {
  char b[48];
  std::snprintf(b, sizeof b, "%.17g", x);
  return b;
}
template <class F> Value translate(F &&f) {
  try {
    return f();
  } catch (const dslvm::Error &) {
    throw;
  } catch (const std::exception &e) {
    throw dslvm::Error(e.what());
  }
}

void check(dslvm::Registry &r, const char *type, const char *name,
           std::vector<std::string> ps, std::string ret, dslvm::Method fn) {
  const std::string who = std::string(type) + "." + name;
  r.method(type, name, ps, ret, [who, ps, fn](void *self, Args &a) {
    for (size_t i = 0; i < ps.size(); ++i) {
      const auto &p = a[i];
      const bool ok = ps[i] == "float"    ? p.kind == Value::FLOAT
                      : ps[i] == "int"    ? p.kind == Value::INT
                      : ps[i] == "string" ? p.kind == Value::STR
                      : ps[i] == "bool"
                          ? p.kind == Value::BOOL
                          : p.kind == Value::OBJ && p.obj && p.type == ps[i];
      if (!ok)
        throw dslvm::Error(who + " argument " + std::to_string(i + 1) +
                           " must be " + ps[i]);
    }
    return translate([&] { return fn(self, a); });
  });
}

int integer(const Value &v, int lo, int hi, const char *what) {
  if (v.i < lo || v.i > hi)
    throw dslvm::Error(std::string(what) + " outside allowed range");
  return static_cast<int>(v.i);
}
void register_host(dslvm::Registry &r, Host &h) {
  r.context("cell", "Cell");
  check(r, "Cell", "sim", {"int"}, "Sim", [&h](void *p, Args &a) {
    (void)h;
    (void)p;
    (void)a;
    if (a[0].i < 0 || a[0].i > 4294967295LL)
      throw dslvm::Error("seed must be in [0,4294967295]");
    Sim s;
    s.config.seed = static_cast<unsigned>(a[0].i);
    return h.hold(h.sims, "Sim", std::move(s));
  });
  check(r, "Sim", "domain", {"float", "float", "float", "float", "float"},
        "Sim", [&h](void *p, Args &a) {
          (void)h;
          (void)p;
          (void)a;
          auto &s = sim(p);
          editable(s);
          s.config.xmin = a[0].f;
          s.config.xmax = a[1].f;
          s.config.ymin = a[2].f;
          s.config.ymax = a[3].f;
          s.config.voxel_size = a[4].f;
          return Value::object("Sim", p);
        });
  check(
      r, "Sim", "substrate", {"string", "float", "float", "float", "float"},
      "Sim", [&h](void *p, Args &a) {
        (void)h;
        (void)p;
        (void)a;
        auto &s = sim(p);
        editable(s);
        if (s.config.substrates.size() >= 32)
          throw dslvm::Error("substrate limit exceeded");
        for (auto &v : s.config.substrates)
          if (v.name == a[0].s)
            throw dslvm::Error("duplicate substrate");
        s.config.substrates.push_back({a[0].s, a[1].f, a[2].f, a[3].f, a[4].f});
        return Value::object("Sim", p);
      });
  check(r, "Sim", "cell_type", {"string"}, "CellType", [&h](void *p, Args &a) {
    (void)h;
    (void)p;
    (void)a;
    auto &s = sim(p);
    editable(s);
    if (a[0].s.empty())
      throw dslvm::Error("cell type name is empty");
    if (s.config.types.size() >= 32)
      throw dslvm::Error("cell type limit exceeded");
    for (auto &v : s.config.types)
      if (v.name == a[0].s)
        throw dslvm::Error("duplicate cell type");
    lmcell::CellType t;
    t.name = a[0].s;
    s.config.types.push_back(t);
    return h.hold(h.types, "CellType", Type{&s, s.config.types.size() - 1});
  });
  check(r, "Sim", "seed", {"string", "int", "float", "float"}, "Sim",
        [&h](void *p, Args &a) {
          (void)h;
          (void)p;
          (void)a;
          auto &s = sim(p);
          editable(s);
          int n = integer(a[1], 0, 100000, "cell count");
          if (s.config.seeds.size() + static_cast<size_t>(n) > 100000)
            throw dslvm::Error("cell count limit exceeded");
          size_t i = 0;
          while (i < s.config.types.size() && s.config.types[i].name != a[0].s)
            ++i;
          if (i == s.config.types.size())
            throw dslvm::Error("unknown cell type: " + a[0].s);
          finite(a[2].f, "seed x");
          finite(a[3].f, "seed y");
          for (int j = 0; j < n; ++j)
            s.config.seeds.push_back({a[2].f, a[3].f, static_cast<double>(i)});
          return Value::object("Sim", p);
        });
  check(r, "Sim", "run", {"float", "float"}, "Result", [&h](void *p, Args &a) {
    (void)h;
    (void)p;
    (void)a;
    auto &s = sim(p);
    editable(s);
    s.config.duration = a[0].f;
    s.config.dt = a[1].f;
    auto out = lmcell::run(s.config);
    s.ran = true;
    return h.hold(h.results, "Result", std::move(out));
  });
  check(r, "CellType", "cycle", {"float"}, "CellType", [&h](void *p, Args &a) {
    (void)h;
    (void)p;
    (void)a;
    auto &t = type(p);
    editable(*t.sim);
    rate(a[0].f, "cycle");
    t.data().cycle_rate = a[0].f;
    return Value::object("CellType", p);
  });
  check(r, "CellType", "death", {"float"}, "CellType", [&h](void *p, Args &a) {
    (void)h;
    (void)p;
    (void)a;
    auto &t = type(p);
    editable(*t.sim);
    rate(a[0].f, "death");
    t.data().death_rate = a[0].f;
    return Value::object("CellType", p);
  });
  check(r, "CellType", "radius", {"float"}, "CellType", [&h](void *p, Args &a) {
    (void)h;
    (void)p;
    (void)a;
    auto &t = type(p);
    editable(*t.sim);
    rate(a[0].f, "radius");
    t.data().radius = a[0].f;
    return Value::object("CellType", p);
  });
  check(r, "CellType", "motility", {"float", "float"}, "CellType",
        [&h](void *p, Args &a) {
          (void)h;
          (void)p;
          (void)a;
          auto &t = type(p);
          editable(*t.sim);
          rate(a[0].f, "speed");
          finite(a[1].f, "persistence");
          if (a[1].f <= 0)
            throw dslvm::Error("persistence must be positive");
          t.data().motility_speed = a[0].f;
          t.data().persistence_time = a[1].f;
          return Value::object("CellType", p);
        });
  check(r, "CellType", "exchange", {"string", "float", "float"}, "CellType",
        [&h](void *p, Args &a) {
          (void)h;
          (void)p;
          (void)a;
          auto &t = type(p);
          editable(*t.sim);
          rate(a[1].f, "uptake");
          rate(a[2].f, "secretion");
          t.data().substrate = a[0].s;
          t.data().uptake_rate = a[1].f;
          t.data().secretion_rate = a[2].f;
          return Value::object("CellType", p);
        });
  check(r, "CellType", "rule", {"string", "string", "string", "float", "float"},
        "CellType", [&h](void *p, Args &a) {
          (void)h;
          (void)p;
          (void)a;
          auto &t = type(p);
          editable(*t.sim);
          if (t.sim->config.rules.size() >= 1000)
            throw dslvm::Error("rule limit exceeded");
          lmcell::Rule r{a[0].s, a[1].s, a[2].s, a[3].f, a[4].f};
          r.type = t.index;
          t.sim->config.rules.push_back(r);
          return Value::object("CellType", p);
        });
  check(r, "Result", "counts", {}, "Matrix", [&h](void *p, Args &a) {
    (void)h;
    (void)p;
    (void)a;
    return h.hold(h.matrices, "Matrix", result(p).counts);
  });
  check(r, "Result", "cells", {}, "Matrix", [&h](void *p, Args &a) {
    (void)h;
    (void)p;
    (void)a;
    return h.hold(h.matrices, "Matrix", result(p).cells);
  });
  check(r, "Result", "profiles", {}, "Matrix", [&h](void *p, Args &a) {
    (void)h;
    (void)p;
    (void)a;
    return h.hold(h.matrices, "Matrix", result(p).profiles);
  });
  check(r, "Result", "live", {}, "int", [&h](void *p, Args &a) {
    (void)h;
    (void)p;
    (void)a;
    auto &m = result(p).counts;
    return Value::integer(static_cast<int64_t>(m.at(m.rows - 1, 1)));
  });
  check(r, "Result", "substrate", {"int"}, "string", [&h](void *p, Args &a) {
    (void)h;
    (void)p;
    (void)a;
    auto &n = result(p).substrate_names;
    auto i = integer(a[0], 0, 31, "substrate index");
    if (static_cast<size_t>(i) >= n.size())
      throw dslvm::Error("substrate index out of range");
    return Value::string(n[i]);
  });
  check(r, "Matrix", "rows", {}, "int", [&h](void *p, Args &a) {
    (void)h;
    (void)p;
    (void)a;
    return Value::integer(static_cast<int64_t>(matrix(p).rows));
  });
  check(r, "Matrix", "cols", {}, "int", [&h](void *p, Args &a) {
    (void)h;
    (void)p;
    (void)a;
    return Value::integer(static_cast<int64_t>(matrix(p).cols));
  });
  check(r, "Matrix", "get", {"int", "int"}, "float", [&h](void *p, Args &a) {
    (void)h;
    (void)p;
    (void)a;
    return Value::real(matrix(p).at(integer(a[0], 0, 10000000, "matrix row"),
                                    integer(a[1], 0, 100, "matrix column")));
  });
  check(r, "Matrix", "save_csv", {"string"}, "void", [&h](void *p, Args &a) {
    (void)h;
    (void)p;
    (void)a;
    std::ofstream f(a[0].s, std::ios::binary);
    if (!f)
      throw dslvm::Error("cannot write CSV: " + a[0].s);
    f.imbue(std::locale::classic());
    f << std::setprecision(17);
    auto &m = matrix(p);
    for (size_t i = 0; i < m.rows; ++i) {
      for (size_t j = 0; j < m.cols; ++j) {
        if (j)
          f << ',';
        f << m.at(i, j);
      }
      f << '\n';
    }
    f.close();
    if (!f)
      throw dslvm::Error("CSV write failed");
    return Value::none();
  });
  check(r, "Cell", "eval", {"string"}, "float", [&h](void *p, Args &a) {
    (void)h;
    (void)p;
    (void)a;
    lmcell::Expr e(a[0].s, {});
    double x = e.eval(nullptr);
    finite(x, "expression result");
    return Value::real(x);
  });
  check(r, "Cell", "assert_close", {"float", "float", "float"}, "void",
        [&h](void *p, Args &a) {
          (void)h;
          (void)p;
          (void)a;
          if (!std::isfinite(a[0].f) || !std::isfinite(a[1].f) ||
              !std::isfinite(a[2].f) || a[2].f < 0 ||
              std::abs(a[0].f - a[1].f) > a[2].f)
            throw dslvm::Error("assert_close failed: " + fmt(a[0].f) + " vs " +
                               fmt(a[1].f));
          return Value::none();
        });
  check(r, "Cell", "assert_true", {"bool"}, "void", [&h](void *p, Args &a) {
    (void)h;
    (void)p;
    (void)a;
    if (!a[0].b)
      throw dslvm::Error("assert_true failed");
    return Value::none();
  });
  check(r, "Cell", "print", {"string"}, "void", [&h](void *p, Args &a) {
    (void)h;
    (void)p;
    (void)a;
    std::cout << a[0].s << '\n';
    return Value::none();
  });
}
const char *usage =
    "usage: lmcell --version | --notices | --dsl-schema "
    "[--json] | --dsl-signatures | [--dry-run] --dsl FILE [FILE ...]\n"
    "environment: LMCELL_MAX_MEMORY_MB  total memory budget in MiB (default "
    "4096); a program that needs more\n"
    "             stops with a run error (exit 3) instead of allocating\n";
} // namespace

int main(int argc, char **argv) {
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
#endif
  Host h;
  dslvm::Registry reg;
  register_host(reg, h);
  if (argc == 2 && std::string(argv[1]) == "--version") {
    std::cout << "lmcell " LMCELL_VERSION "\n";
    return 0;
  }
  if (argc == 2 && std::string(argv[1]) == "--notices") {
    std::cout.write(reinterpret_cast<const char *>(lmcell::embedded_notices),
                    (std::streamsize)lmcell::embedded_notices_size);
    return 0;
  }
  if (argc >= 2 && std::string(argv[1]) == "--dsl-schema") {
    if (argc == 2) {
      for (const auto &s : reg.signatures())
        std::cout << s << '\n';
      return 0;
    }
    if (argc == 3 && std::string(argv[2]) == "--json") {
      std::cout.write(reinterpret_cast<const char *>(lmcell::embedded_schema),
                      (std::streamsize)lmcell::embedded_schema_size);
      return 0;
    }
    std::cerr << usage;
    return 2;
  }
  if (argc == 2 && std::string(argv[1]) == "--dsl-signatures") {
    for (const auto &s : reg.signatures())
      std::cout << s << '\n';
    return 0;
  }
  bool dry = false;
  int dsl_arg = 1;
  if (argc > 1 && std::string(argv[dsl_arg]) == "--dry-run") {
    dry = true;
    ++dsl_arg;
  }
  if (argc <= dsl_arg || std::string(argv[dsl_arg]) != "--dsl") {
    std::cerr << usage;
    return 2;
  }
  std::vector<std::string> files;
  for (int i = dsl_arg + 1; i < argc; i++) {
    if (std::string(argv[i]) == "--dry-run") {
      if (dry) {
        std::cerr << usage;
        return 2;
      }
      dry = true;
    } else if (std::string(argv[i]).empty() ||
               std::string(argv[i]).rfind("--", 0) == 0) {
      std::cerr << usage;
      return 2;
    } else
      files.push_back(argv[i]);
  }
  if (files.empty()) {
    std::cerr << usage;
    return 2;
  }
  if (!membudget::configure("lmcell", "LMCELL_MAX_MEMORY_MB")) {
    std::cerr << usage;
    return 2;
  }
  const std::string schema(
      reinterpret_cast<const char *>(lmcell::embedded_schema),
      lmcell::embedded_schema_size);
  dslvm::Context ctx;
  Cell cell;
  ctx.set("cell", &cell);
  std::vector<dslvm::Program> programs;
  for (const auto &file : files)
    try {
      programs.push_back(
          dslvm::Program::load(dslvm::read_file(file), schema, reg));
    } catch (const dslvm::Error &e) {
      return dslvm::report_error("load", e);
    } catch (const std::exception &e) {
      return dslvm::report_error("load", dslvm::Error(e.what()));
    }
  if (dry) {
    dslvm::report_ok();
    return 0;
  }
  for (const auto &p : programs) {
    try {
      p.run(ctx, reg);
    } catch (const dslvm::Error &e) {
      return dslvm::report_error("run", e);
    } catch (const std::exception &e) { // the budget, raised inside the VM
      return dslvm::report_error("run", dslvm::Error(e.what()));
    }
    dslvm::report_ok();
  }
  return 0;
}
