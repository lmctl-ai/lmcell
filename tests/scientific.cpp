#define _USE_MATH_DEFINES
#include "PhysiCell.h"
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
    } else if (mode == "increase") {
      // Oxygen at half-max with an increasing rule multiplies cycle by 1.5.
      constexpr int n = 10000;
      t.cycle_rate = .3;
      c.substrates.push_back({"oxygen", 0, 0, 1, -1});
      c.rules.push_back({"oxygen", "increases", "cycle entry", 1, 2, 0});
      c.types.push_back(t);
      for (int i = 0; i < n; ++i)
        c.seeds.push_back({0, 0, 0});
      auto r = run(c);
      const double factor = std::exp(.45 * 2);
      near(r.counts.at(r.counts.rows - 1, 1), n * factor,
           3 * std::sqrt(n * factor * (factor - 1)) + n * .005,
           "increasing Hill rule at half-max scales cycle rate by 1.5");
      std::cout << "increase final=" << r.counts.at(r.counts.rows - 1, 1)
                << " expected=" << n * factor << '\n';
    } else if (mode == "secretion") {
      // One cell: drho/dt = c*(S*(1-rho) - U*rho), c = Vcell/Vvoxel.
      c.xmin = 0;
      c.xmax = 10;
      c.ymin = 0;
      c.ymax = 10;
      c.voxel_size = 10;
      c.dt = .01;
      c.substrates.push_back({"signal", 0, 0, 0, -1});
      t.radius = 2;
      t.exchanges.push_back({"signal", 1, 3});
      c.types.push_back(t);
      c.seeds.push_back({5, 5, 0});
      c.duration = 20;
      auto r = run(c);
      const double cv = 4.0 / 3 * 3.14159265358979323846 * 8 / 1000;
      const double a = c.dt * cv;
      // Implicit upstream step: rho' = (rho + a*S) / (1 + a*(S+U)).
      double rho = 0;
      for (int i = 0; i < 2000; ++i)
        rho = (rho + a * 3) / (1 + a * 4);
      near(r.profiles.at(0, 2), rho, 1e-12, "implicit secretion/uptake step");
      near(r.profiles.at(0, 2), .75 * (1 - std::exp(-4 * cv * 20)), 2e-4,
           "secretion/uptake relaxation to S/(S+U)");
    } else if (mode == "speed") {
      // A decreasing migration-speed rule at half-max halves speed: MSD/4.
      constexpr int n = 4000;
      c.duration = 20;
      c.dt = .05;
      t.motility_speed = 1;
      t.persistence_time = 1;
      c.substrates.push_back({"cue", 0, 0, 1, -1});
      c.rules.push_back({"cue", "decreases", "migration speed", 1, 1, 0});
      c.types.push_back(t);
      for (int i = 0; i < n; ++i)
        c.seeds.push_back({0, 0, 0});
      auto r = run(c);
      double sum = 0, sum2 = 0;
      for (size_t i = 0; i < r.cells.rows; ++i) {
        double q = r.cells.at(i, 0) * r.cells.at(i, 0) +
                   r.cells.at(i, 1) * r.cells.at(i, 1);
        sum += q;
        sum2 += q * q;
      }
      double msd = sum / n, se = std::sqrt((sum2 / n - msd * msd) / n);
      double expected = 400;
      for (int k = 1; k < 400; ++k)
        expected += 2 * (400 - k) * std::pow(1 - c.dt, k);
      expected *= .25 * c.dt * c.dt;
      near(msd, expected, 3 * se, "half-speed persistent walk MSD");
      std::cout << "speed MSD=" << msd << " expected=" << expected
                << " SE=" << se << '\n';
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
      // A no-flux, nondecaying companion must not inherit the fixed edges.
      c.substrates.push_back({"inert", 1, 0, 2, -1});
      auto r = run(c);
      for (size_t i = 0; i < r.profiles.rows; ++i)
        near(r.profiles.at(i, 3), 2, 1e-9, "no-flux substrate beside Dirichlet");
      double k = .1, L = 199;
      for (size_t i = 0; i < r.profiles.rows; ++i) {
        double x = r.profiles.at(i, 0) - .5;
        double exact = std::cosh(k * (x - L / 2)) / std::cosh(k * L / 2);
        near(r.profiles.at(i, 2), exact, .0015,
             "diffusion-decay steady cosh profile");
      }
      near(r.profiles.at(10, 2), std::exp(-1.0), .0015,
           "semi-infinite exponential penetration");
    } else if (mode == "edges") {
      // True 2D fixed-boundary diffusion. Square plate, one hot side at 1,
      // three cold sides at 0, no decay: by superposition of the four
      // rotated problems the steady centre value is exactly 1/4.
      c.xmin = 0;
      c.xmax = 105;
      c.ymin = 0;
      c.ymax = 105;
      c.voxel_size = 5;
      // BioFVM re-pins edge voxels between LOD sweeps; the steady error
      // scales with D*dt/h^2, so keep that ratio at 0.01.
      c.duration = 1500;
      c.dt = .025;
      c.substrates.push_back({"heat", 10, 0, 0, -1});
      c.edges.push_back({"heat", "xmin", 1});
      c.edges.push_back({"heat", "xmax", 0});
      c.edges.push_back({"heat", "ymin", 0});
      c.edges.push_back({"heat", "ymax", 0});
      auto r = run(c);
      check(r.profiles.rows == 441, "21x21 voxels");
      // Voxel (10,10) is the centre; x-fastest ordering.
      near(r.profiles.at(10 + 21 * 10, 0), 52.5, 0, "centre voxel x");
      near(r.profiles.at(10 + 21 * 10, 2), .25, .004, "2D Laplace centre 1/4");
      // Mirror symmetry about y = centre.
      for (int i = 0; i < 21; ++i)
        for (int j = 0; j < 21; ++j)
          near(r.profiles.at(i + 21 * j, 2), r.profiles.at(i + 21 * (20 - j), 2),
               1e-9, "mirror symmetry in y");
      // Series solution at quarter points on the centre line (continuous
      // square of side 100 between the pinned voxel centres).
      for (int i : {5, 15}) {
        double x = (i * 5.0) / 100.0, y = .5, sum = 0;
        for (int n = 1; n < 200; n += 2)
          sum += 4 / (n * M_PI) * std::sin(n * M_PI * y) *
                 std::sinh(n * M_PI * (1 - x)) / std::sinh(n * M_PI);
        near(r.profiles.at(i + 21 * 10, 2), sum, .01, "2D Laplace series");
      }
      std::cout << "edges centre=" << r.profiles.at(220, 2) << '\n';
    } else if (mode == "gradient") {
      // A linear steady gradient between a fixed xmin and xmax edge.
      c.xmin = 0;
      c.xmax = 200;
      c.ymin = 0;
      c.ymax = 40;
      c.voxel_size = 10;
      c.duration = 3000;
      if (argc > 2) { // gradient substep: coarse cell step, fine diffusion
        c.dt = 1;
        c.diffusion_dt = .05;
      } else
        c.dt = .05; // D*dt/h^2 = 0.01 (see edges)
      c.substrates.push_back({"cue", 20, 0, 0, -1});
      c.edges.push_back({"cue", "xmin", 0});
      c.edges.push_back({"cue", "xmax", 1});
      auto r = run(c);
      for (size_t i = 0; i < r.profiles.rows; ++i)
        near(r.profiles.at(i, 2), (r.profiles.at(i, 0) - 5) / 190, 2e-3,
             "linear steady profile between fixed edges");
    } else if (mode == "chemotaxis") {
      // Direction re-drawn every step (persistence < dt): mean velocity up
      // the gradient is speed*E[(b+(1-b)cos)/|b e+(1-b)u|]; 1 for b=1 and
      // 2/pi for b=1/2. A history snapshot after the gradient forms gives the
      // drift over a known window.
      c.xmin = 0;
      c.xmax = 1000;
      c.ymin = 0;
      c.ymax = 1000;
      c.voxel_size = 20;
      c.duration = 200;
      c.dt = .1;
      c.record_interval = 50;
      c.substrates.push_back({"cue", 10000, 0, 0, -1});
      c.edges.push_back({"cue", "xmin", 0});
      c.edges.push_back({"cue", "xmax", 1});
      CellType full = t, half = t;
      full.name = "full";
      half.name = "half";
      for (auto *q : {&full, &half}) {
        q->motility_speed = 1;
        q->persistence_time = .01;
        q->chemotaxis = "cue";
      }
      full.chemotaxis_bias = 1;
      half.chemotaxis_bias = .5;
      c.types = {full, half};
      constexpr int n = 2000;
      for (int i = 0; i < n; ++i) {
        c.seeds.push_back({500, 500, 0});
        c.seeds.push_back({500, 500, 1});
      }
      auto r = run(c);
      check(r.history.rows == 5 * 2 * n, "five snapshots of every cell");
      double x1[2] = {0, 0}, x4[2] = {0, 0}, y4[2] = {0, 0};
      for (size_t i = 0; i < r.history.rows; ++i) {
        const int ty = static_cast<int>(r.history.at(i, 3));
        if (r.history.at(i, 0) == 50)
          x1[ty] += r.history.at(i, 1) / n;
        if (r.history.at(i, 0) == 200) {
          x4[ty] += r.history.at(i, 1) / n;
          y4[ty] += r.history.at(i, 2) / n;
        }
      }
      near(x4[0] - x1[0], 150, .5, "full bias: straight up the gradient");
      near(x4[1] - x1[1], 150 * 2 / M_PI, 1.5, "half bias drift 2/pi");
      near(y4[1], 500, 1.5, "no drift across the gradient");
      check(r.type_counts.cols == 3 && r.type_counts.at(0, 1) == n &&
                r.type_counts.at(0, 2) == n,
            "per-type counts");
      std::cout << "chemotaxis drift full=" << x4[0] - x1[0]
                << " half=" << x4[1] - x1[1] << '\n';
    } else if (mode == "toward") {
      // PhysiCell-style saturation: zero base cycle rate, signal at
      // half-max, saturation 0.3 -> rate 0.15 (an increasing rule could not
      // lift a zero base).
      constexpr int n = 10000;
      c.substrates.push_back({"growth_factor", 0, 0, 1, -1});
      Rule rule{"growth_factor", "toward", "cycle entry", 1, 2, 0};
      rule.saturation = .3;
      c.rules.push_back(rule);
      c.types.push_back(t);
      for (int i = 0; i < n; ++i)
        c.seeds.push_back({0, 0, 0});
      auto r = run(c);
      const double factor = std::exp(.15 * 2);
      near(r.counts.at(r.counts.rows - 1, 1), n * factor,
           3 * std::sqrt(n * factor * (factor - 1)) + n * .005,
           "rule toward saturation from a zero base");
    } else if (mode == "crowding") {
      // Contact inhibition in one voxel: cycle 0.1*(1-H(N-1)), death 0.02.
      // Births balance deaths at H=0.8, i.e. N-1 = K*4^(1/n) = 100.
      c.xmin = 0;
      c.xmax = 20;
      c.ymin = 0;
      c.ymax = 20;
      c.voxel_size = 20;
      c.duration = 1500;
      c.dt = .5;
      t.cycle_rate = .1;
      t.death_rate = .02;
      Rule rule{kNeighbors, "toward", "cycle entry", 50, 2, 0};
      rule.saturation = 0;
      c.rules.push_back(rule);
      c.types.push_back(t);
      for (int i = 0; i < 10; ++i)
        c.seeds.push_back({10, 10, 0});
      auto r = run(c);
      double mean = 0;
      int m = 0;
      for (size_t i = 0; i < r.counts.rows; ++i)
        if (r.counts.at(i, 0) >= 500) {
          mean += r.counts.at(i, 1);
          ++m;
        }
      mean /= m;
      near(mean, 101, 6, "contact inhibition carrying capacity");
      std::cout << "crowding mean=" << mean << " expected=101\n";
    } else if (mode == "multiexchange") {
      // One type consumes food and secretes a signal at once.
      c.xmin = 0;
      c.xmax = 10;
      c.ymin = 0;
      c.ymax = 10;
      c.voxel_size = 10;
      c.duration = 1;
      c.dt = .01;
      c.substrates.push_back({"food", 0, 0, 1, -1});
      c.substrates.push_back({"signal", 0, 0, 0, -1});
      t.exchanges.push_back({"food", 2, 0});
      t.exchanges.push_back({"signal", 0, 3});
      c.types.push_back(t);
      c.seeds.push_back({5, 5, 0});
      auto r = run(c);
      const double cv = 4.0 / 3 * 3.14159265358979323846 / 1000;
      near(r.profiles.at(0, 2), std::exp(-2 * cv), 1e-6, "uptake of food");
      near(r.profiles.at(0, 3), 1 - std::exp(-3 * cv), 1e-5,
           "secretion of signal toward saturation 1");
    } else if (mode == "exchange") {
      PhysiCell::cell_defaults.phenotype.secretion.pMicroenvironment = nullptr;
      c.xmin = 0;
      c.xmax = 10;
      c.ymin = 0;
      c.ymax = 10;
      c.voxel_size = 10;
      c.duration = 1;
      c.dt = .01;
      c.substrates.push_back({"food", 0, 0, 1, -1});
      t.exchanges.push_back({"food", 2, 0});
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
      b = c;
      b.substrates.push_back({"o", 1, 0, 1, -1});
      b.types.push_back(t);
      b.types[0].exchanges = {{"o", 1, 0}, {"o", 0, 1}};
      bad(b); // a second exchange for the same substrate
      b.types[0].exchanges.clear();
      b.types[0].chemotaxis = "o";
      b.types[0].chemotaxis_bias = 1.5;
      bad(b);
      b = c;
      b.substrates.push_back({kNeighbors, 1, 0, 1, -1});
      bad(b);
      b = c;
      b.substrates.push_back({"o", 1, 0, 1, -1});
      b.edges.push_back({"o", "left", 1});
      bad(b);
      b.edges = {{"o", "xmin", -2}};
      bad(b);
      b = c;
      b.ymax = b.ymin + b.voxel_size;
      b.substrates.push_back({"o", 1, 0, 1, -1});
      b.edges.push_back({"o", "ymin", 1});
      bad(b);
      b = c;
      b.record_interval = -1;
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
