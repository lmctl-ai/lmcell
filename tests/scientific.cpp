#include "core.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace lmcell;
int checks = 0;
void check(bool b, const char *m) {
  ++checks;
  if (!b)
    throw std::runtime_error(m);
}
void near(double a, double b, double tol, const char *m) {
  ++checks;
  if (!std::isfinite(a) || std::abs(a - b) > tol) {
    std::cerr << m << ": " << a << " expected " << b << " tolerance " << tol
              << '\n';
    throw std::runtime_error(m);
  }
}
int main(int argc, char **argv) {
  try {
    const std::string mode = argc > 1 ? argv[1] : "baseline";
    Config c;
    c.xmin = -100;
    c.xmax = 100;
    c.ymin = -100;
    c.ymax = 100;
    c.voxel_size = 20;
    c.duration = 2;
    c.dt = .01;
    c.seed = 12345;
    CellType t;
    t.name = "a";
    t.radius = 1;
    if (mode == "baseline") {
      c.substrates.push_back({"a", 0, .25, 2, -1});
      c.types.push_back(t);
      c.seeds.push_back({0, 0, 0});
      auto r = run(c);
      near(r.counts.at(r.counts.rows - 1, 0), 2, 0, "end time");
      near(r.counts.at(r.counts.rows - 1, 1), 1, 0, "quiescent population");
      near(r.cells.at(0, 0), 0, 0, "nonmotile x");
      near(r.cells.at(0, 1), 0, 0, "nonmotile y");
      for (size_t i = 0; i < r.profiles.rows; ++i)
        near(r.profiles.at(i, 2), 2 * std::exp(-.5), .0005,
             "uniform diffusion-decay");
      try {
        r.cells.at(1, 0);
        check(false, "bounds rejected");
      } catch (const std::out_of_range &) {
        ++checks;
      }
      try {
        run(c);
        check(false, "global reuse rejected");
      } catch (const std::runtime_error &) {
        ++checks;
      }
    } else if (mode == "growth" || mode == "death" || mode == "rules") {
      constexpr int n = 10000;
      c.duration = 2;
      c.dt = .01;
      if (mode == "growth")
        t.cycle_rate = .3;
      else
        t.death_rate = .3;
      if (mode == "rules") {
        c.substrates.push_back({"oxygen", 0, 0, 1, -1});
        c.rules.push_back({"oxygen", "decreases", "apoptosis", 1, 2, 0});
      }
      c.types.push_back(t);
      for (int i = 0; i < n; ++i)
        c.seeds.push_back({0, 0, 0});
      auto r = run(c);
      double factor = mode == "growth" ? std::exp(.6)
                                       : std::exp(mode == "rules" ? -.3 : -.6);
      double variance = mode == "growth" ? n * factor * (factor - 1)
                                         : n * factor * (1 - factor);
      near(r.counts.at(r.counts.rows - 1, 1), n * factor,
           3 * std::sqrt(variance) + n * .003,
           "population exponential law within 3SE plus timestep error");
      if (mode != "growth")
        check(r.cells.rows == n, "apoptotic cells retained before clearance");
      std::cout << mode << " final=" << r.counts.at(r.counts.rows - 1, 1)
                << " expected=" << n * factor << '\n';
    } else if (mode == "walk") {
      constexpr int n = 6000;
      c.duration = 20;
      c.dt = .05;
      t.motility_speed = 1;
      t.persistence_time = 1;
      c.types.push_back(t);
      for (int i = 0; i < n; ++i)
        c.seeds.push_back({0, 0, 0});
      auto r = run(c);
      double sum = 0, sum2 = 0, mx = 0, my = 0;
      for (size_t i = 0; i < r.cells.rows; ++i) {
        double x = r.cells.at(i, 0), y = r.cells.at(i, 1), q = x * x + y * y;
        sum += q;
        sum2 += q * q;
        mx += x;
        my += y;
      }
      double msd = sum / n, se = std::sqrt((sum2 / n - msd * msd) / n);
      // Correlation of the discrete native persistent walk is q^k, q=1-dt/tau.
      int steps = 400;
      double expected = steps;
      for (int k = 1; k < steps; ++k)
        expected +=
            2 * (steps - k) * std::pow(1 - c.dt / t.persistence_time, k);
      expected *= c.dt * c.dt;
      near(msd, expected, 3 * se, "persistent walk discrete MSD");
      near(msd, 2 * (20 - 1 + std::exp(-20.0)), 3 * se + 1.1,
           "MSD approaches 4Dt with D=v^2*tau/2");
      near(mx / n, 0, 3 * std::sqrt(expected / (2 * n)), "isotropic x");
      near(my / n, 0, 3 * std::sqrt(expected / (2 * n)), "isotropic y");
      std::cout << "walk MSD=" << msd << " expected=" << expected
                << " SE=" << se << '\n';
    } else if (mode == "diffusion") {
      c.xmin = 0;
      c.xmax = 200;
      c.ymin = 0;
      c.ymax = 1;
      c.voxel_size = 1;
      c.duration = 1000;
      c.dt = .01;
      c.substrates.push_back({"oxygen", 1, .01, 0, 1});
      auto r = run(c);
      double k = .1, L = 199;
      for (size_t i = 0; i < r.profiles.rows; ++i) {
        double x = r.profiles.at(i, 0) - .5;
        double exact = std::cosh(k * (x - L / 2)) / std::cosh(k * L / 2);
        near(r.profiles.at(i, 2), exact, .0015,
             "diffusion-decay steady cosh profile");
      }
      near(r.profiles.at(10, 2), std::exp(-1.0), .0015,
           "semi-infinite exponential penetration");
    } else if (mode == "exchange") {
      c.xmin = 0;
      c.xmax = 10;
      c.ymin = 0;
      c.ymax = 10;
      c.voxel_size = 10;
      c.duration = 1;
      c.dt = .01;
      c.substrates.push_back({"food", 0, 0, 1, -1});
      t.substrate = "food";
      t.uptake_rate = 2;
      c.types.push_back(t);
      c.seeds.push_back({5, 5, 0});
      auto r = run(c);
      double rate = 2 * (4.0 / 3 * 3.14159265358979323846) / 1000;
      near(r.profiles.at(0, 2), std::exp(-rate), .000001,
           "cell volume normalized substrate uptake");
    } else if (mode == "validation") {
      auto bad = [&](Config v) {
        try {
          run(v);
          check(false, "invalid configuration accepted");
        } catch (const std::invalid_argument &) {
          ++checks;
        }
      };
      Config b = c;
      b.dt = 0;
      bad(b);
      b = c;
      b.dt = std::nan("");
      bad(b);
      b = c;
      b.xmax = b.xmin;
      bad(b);
      b = c;
      b.voxel_size = .000001;
      bad(b);
      b = c;
      b.types.push_back(t);
      b.types[0].radius = std::nan("");
      bad(b);
      b = c;
      b.substrates.push_back({"o", -1, 0, 1, -1});
      bad(b);
      b = c;
      b.seeds.push_back({0, 0, 20});
      bad(b);
      b = c;
      b.rules.push_back({"unknown", "bad", "cycle entry", 1, 2});
      bad(b);
    } else if (mode == "partial") {
      c.duration = .25;
      c.dt = .1;
      c.substrates.push_back({"a", 0, .25, 2, -1});
      auto r = run(c);
      check(r.counts.rows == 4, "partial step rows");
      near(r.counts.at(3, 0), .25, 0, "partial step ends exactly");
      near(r.profiles.at(0, 2), 2 * std::exp(-.25 * .25), .0007,
           "partial-duration decay uses actual uniform dt");
    } else
      throw std::runtime_error("unknown mode");
    std::cout << checks << " " << mode << " checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL " << e.what() << '\n';
    return 1;
  }
}
