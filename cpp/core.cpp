#include "core.hpp"
#include "memory_budget.hpp"
#include "BioFVM.h"
#include "PhysiCell.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>

namespace lmcell {
namespace {
bool used = false;
// Budget pre-checks (memory_budget.hpp), measured on x86_64 Linux: a 2D BioFVM grid with its
// cell container and LOD solver scratch peaks at about 680 + 94 * substrates bytes per voxel;
// one PhysiCell cell with its phenotype takes about 4.6 KiB. Checking before PhysiCell builds
// them keeps a budget failure out of half-initialized global state.
constexpr std::size_t kVoxelBytes = 768, kVoxelSubstrateBytes = 96;
constexpr std::size_t kCellBytes = 16 * 1024;
void finite(double x, const char *n) {
  if (!std::isfinite(x))
    throw std::invalid_argument(std::string(n) + " must be finite");
}
void nonnegative(double x, const char *n) {
  finite(x, n);
  if (x < 0)
    throw std::invalid_argument(std::string(n) + " must be nonnegative");
}
double response(double x, double k, double n) {
  if (x <= 0)
    return 0;
  if (k == 0)
    return 1;
  // Logistic form avoids inf/inf for strong signals and large Hill powers.
  const double z = n * (std::log(k) - std::log(x));
  return z >= 0 ? std::exp(-z) / (1 + std::exp(-z)) : 1 / (1 + std::exp(z));
}
} // namespace
double Matrix::at(std::size_t i, std::size_t j) const {
  if (i >= rows || j >= cols || i * cols + j >= values.size())
    throw std::out_of_range("matrix index out of range");
  return values[i * cols + j];
}
Results run(const Config &cfg) {
  using namespace PhysiCell;
  using namespace BioFVM;
  for (double x : {cfg.xmin, cfg.xmax, cfg.ymin, cfg.ymax, cfg.voxel_size,
                   cfg.duration, cfg.dt})
    finite(x, "domain/time");
  if (cfg.dt <= 0 || cfg.duration < 0)
    throw std::invalid_argument("dt must be positive and duration nonnegative");
  if (cfg.xmax <= cfg.xmin || cfg.ymax <= cfg.ymin || cfg.voxel_size <= 0)
    throw std::invalid_argument(
        "domain bounds and voxel size must be positive");
  const double wx = cfg.xmax - cfg.xmin, wy = cfg.ymax - cfg.ymin;
  const double rx = wx / cfg.voxel_size, ry = wy / cfg.voxel_size;
  if (!std::isfinite(rx) || !std::isfinite(ry) || rx < 1 || ry < 1 ||
      std::abs(rx - std::round(rx)) > 1e-9 ||
      std::abs(ry - std::round(ry)) > 1e-9)
    throw std::invalid_argument(
        "domain lengths must be integer multiples of voxel size");
  if (!std::isfinite(cfg.voxel_size * cfg.voxel_size * cfg.voxel_size))
    throw std::invalid_argument("domain voxel volume must be finite");
  const double nx = std::ceil((cfg.xmax - cfg.xmin) / cfg.voxel_size),
               ny = std::ceil((cfg.ymax - cfg.ymin) / cfg.voxel_size);
  const double steps_d = std::ceil(cfg.duration / cfg.dt);
  finite(cfg.diffusion_dt, "diffusion dt");
  if (cfg.diffusion_dt < 0)
    throw std::invalid_argument("diffusion dt must be positive");
  const double sub_d =
      cfg.diffusion_dt > 0 && steps_d > 0
          ? std::max(1.0, std::ceil(cfg.duration / steps_d / cfg.diffusion_dt -
                                    1e-9))
          : 1.0;
  if (!std::isfinite(nx * ny) || nx * ny > 1000000 || steps_d > 1000000 ||
      sub_d > 100000 ||
      nx * ny * std::max(1.0, steps_d) * sub_d > 200000000)
    throw std::invalid_argument("domain/time resource limit exceeded");
  if (cfg.types.size() > 32 || cfg.substrates.size() > 32 ||
      cfg.seeds.size() > 100000 || cfg.rules.size() > 1000)
    throw std::invalid_argument("configuration resource limit exceeded");
  const size_t nt = std::max(size_t(1), cfg.types.size());
  std::set<std::string> names;
  for (const auto &s : cfg.substrates) {
    if (s.name.empty() || !names.insert(s.name).second)
      throw std::invalid_argument(
          "substrate names must be unique and nonempty");
    if (s.name == kNeighbors)
      throw std::invalid_argument(
          "substrate name 'neighbors' is reserved for the crowding signal");
    nonnegative(s.diffusion, "diffusion");
    nonnegative(s.decay, "decay");
    nonnegative(s.initial, "initial concentration");
    finite(s.boundary, "boundary");
    if (s.boundary < 0 && s.boundary != -1)
      throw std::invalid_argument(
          "boundary must be nonnegative or -1 for no flux");
  }
  std::set<std::string> type_names;
  for (const auto &t : cfg.types) {
    if (t.name.empty() || !type_names.insert(t.name).second)
      throw std::invalid_argument(
          "cell type names must be unique and nonempty");
    nonnegative(t.cycle_rate, "cycle rate");
    nonnegative(t.death_rate, "death rate");
    nonnegative(t.motility_speed, "motility speed");
    finite(t.persistence_time, "persistence");
    finite(t.radius, "radius");
    if (t.persistence_time <= 0 || t.radius <= 0 ||
        !std::isfinite(t.radius * t.radius * t.radius))
      throw std::invalid_argument("persistence and radius must be positive");
    std::set<std::string> exchanged;
    for (const auto &e : t.exchanges) {
      nonnegative(e.uptake, "uptake");
      nonnegative(e.secretion, "secretion");
      if (!names.count(e.substrate))
        throw std::invalid_argument("unknown exchange substrate");
      if (!exchanged.insert(e.substrate).second)
        throw std::invalid_argument("exchange for substrate '" + e.substrate +
                                    "' is set twice on cell type '" + t.name +
                                    "'");
    }
    if (!t.chemotaxis.empty() && !names.count(t.chemotaxis))
      throw std::invalid_argument("unknown chemotaxis substrate");
    finite(t.chemotaxis_bias, "chemotaxis bias");
    if (t.chemotaxis_bias < 0 || t.chemotaxis_bias > 1)
      throw std::invalid_argument("chemotaxis bias must be in [0,1]");
  }
  for (const auto &r : cfg.rules) {
    if (!names.count(r.substrate) && r.substrate != kNeighbors)
      throw std::invalid_argument("unknown rule substrate (signal must be a "
                                  "declared substrate or 'neighbors')");
    if (r.type >= nt)
      throw std::invalid_argument("rule type index out of range");
    if (r.effect != "increases" && r.effect != "decreases" &&
        r.effect != "toward")
      throw std::invalid_argument(
          "rule effect must be increases, decreases or toward");
    nonnegative(r.saturation, "rule saturation");
    if (r.target != "cycle entry" && r.target != "cycle rate" &&
        r.target != "apoptosis" && r.target != "death rate" &&
        r.target != "migration speed")
      throw std::invalid_argument("unsupported rule target");
    nonnegative(r.half_max, "rule half_max");
    finite(r.hill, "rule hill");
    if (r.hill <= 0)
      throw std::invalid_argument("rule hill must be positive");
  }
  for (const auto &p : cfg.seeds) {
    if (p.size() != 3)
      throw std::invalid_argument("seed must contain x,y,type");
    for (double x : p)
      finite(x, "seed");
    if (p[2] < 0 || p[2] >= nt || p[2] != std::floor(p[2]))
      throw std::invalid_argument("seed cell type index out of range");
    if (p[0] < cfg.xmin || p[0] >= cfg.xmax || p[1] < cfg.ymin ||
        p[1] >= cfg.ymax)
      throw std::invalid_argument("seed outside domain");
  }
  for (const auto &e : cfg.edges) {
    if (!names.count(e.substrate))
      throw std::invalid_argument("unknown edge substrate");
    if (e.side != "xmin" && e.side != "xmax" && e.side != "ymin" &&
        e.side != "ymax")
      throw std::invalid_argument("edge side must be xmin, xmax, ymin or ymax");
    if (ny <= 1 && (e.side == "ymin" || e.side == "ymax"))
      throw std::invalid_argument("a single-row domain has no y edges");
    finite(e.value, "edge value");
    if (e.value < 0 && e.value != -1)
      throw std::invalid_argument(
          "edge value must be nonnegative or -1 for no flux");
  }
  finite(cfg.record_interval, "record interval");
  if (cfg.record_interval < 0)
    throw std::invalid_argument("record interval must be nonnegative");
  const double records =
      cfg.record_interval > 0 ? std::floor(cfg.duration / cfg.record_interval) + 1
                              : 0;
  if (records * nx * ny * (2.0 + cfg.substrates.size()) > 20000000)
    throw std::invalid_argument("profile history resource limit exceeded");
  if (used)
    throw std::runtime_error(
        "lmcell supports one PhysiCell simulation per process");
  used = true; // Failed native initialization also consumes the global state.
  // Upstream reports progress on std::cout (for example the selected solver).
  // Host stdout carries only the JSON result line, so route it to stderr.
  struct CoutToCerr {
    std::streambuf *old = std::cout.rdbuf(std::cerr.rdbuf());
    ~CoutToCerr() { std::cout.rdbuf(old); }
  } quiet_stdout;
  membudget::require(static_cast<std::size_t>(nx * ny) *
                     (kVoxelBytes + kVoxelSubstrateBytes *
                                        std::max<std::size_t>(1, cfg.substrates.size())));
  microenvironment.name = "lmcell";
  set_default_microenvironment(&microenvironment);
  default_microenvironment_options.simulate_2D = true;
  // Configure densities before resizing: BioFVM sizes solver storage on resize.
  if (cfg.substrates.empty())
    microenvironment.set_density(0, "inert", "arbitrary", 0, 0);
  for (size_t i = 0; i < cfg.substrates.size(); ++i) {
    const auto &s = cfg.substrates[i];
    if (i == 0)
      microenvironment.set_density(0, s.name, "arbitrary", s.diffusion,
                                   s.decay);
    else
      microenvironment.add_density(s.name, "arbitrary", s.diffusion, s.decay);
  }
  microenvironment.resize_space(cfg.xmin, cfg.xmax, cfg.ymin, cfg.ymax,
                                -cfg.voxel_size / 2, cfg.voxel_size / 2,
                                cfg.voxel_size, cfg.voxel_size, cfg.voxel_size);
  microenvironment.diffusion_decay_solver =
      diffusion_decay_solver__constant_coefficients_LOD_2D;
  for (unsigned v = 0; v < microenvironment.number_of_voxels(); ++v) {
    const auto ijk = microenvironment.mesh.cartesian_indices(v);
    const bool on[4] = {ijk[0] == 0, ijk[0] + 1 == static_cast<unsigned>(nx),
                        ny > 1 && ijk[1] == 0,
                        ny > 1 && ijk[1] + 1 == static_cast<unsigned>(ny)};
    for (size_t i = 0; i < cfg.substrates.size(); ++i) {
      const auto &s = cfg.substrates[i];
      microenvironment.density_vector(v)[i] = s.initial;
      // Global edge value first, then per-side overrides in call order.
      double pin = (on[0] || on[1] || on[2] || on[3]) ? s.boundary : -1;
      for (const auto &e : cfg.edges)
        if (e.substrate == s.name) {
          const int side = e.side == "xmin"   ? 0
                           : e.side == "xmax" ? 1
                           : e.side == "ymin" ? 2
                                              : 3;
          if (on[side])
            pin = e.value;
        }
      if (pin >= 0)
        microenvironment.update_dirichlet_node(v, static_cast<int>(i), pin);
    }
  }
  microenvironment.apply_dirichlet_conditions();
  std::unique_ptr<Cell_Container> container(
      create_cell_container_for_microenvironment(microenvironment,
                                                 cfg.voxel_size));
  initialize_default_cell_definition();
  physicell_random_seeds.assign(1, static_cast<unsigned>(cfg.seed));
  cell_division_orientation = UniformOnUnitCircle;
  std::vector<std::unique_ptr<Cell_Definition>> defs;
  for (size_t i = 0; i < nt; ++i) {
    defs.push_back(std::make_unique<Cell_Definition>(cell_defaults));
    auto &d = *defs.back();
    d.is_movable = true; // The upstream copy constructor omits this field.
    d.type = static_cast<int>(i);
    d.name = cfg.types.empty() ? "cell" : cfg.types[i].name;
    d.functions.update_phenotype = nullptr;
    d.functions.volume_update_function = nullptr;
    d.functions.cycle_model = live;
    d.phenotype.cycle.sync_to_cycle_model(live);
    auto &ph = d.phenotype;
    // The global default phenotype may have been constructed before BioFVM.
    // Bind explicitly: its sync_to_current method retains a null stored
    // pointer.
    ph.secretion.sync_to_microenvironment(&microenvironment);
    const auto t = cfg.types.empty() ? CellType{} : cfg.types[i];
    ph.cycle.data.transition_rate(0, 0) = t.cycle_rate;
    for (auto &r : ph.death.rates)
      r = 0;
    ph.death.rates[ph.death.find_death_model_index(
        PhysiCell_constants::apoptosis_death_model)] = t.death_rate;
    ph.motility.is_motile = t.motility_speed > 0;
    ph.motility.restrict_to_2D = true;
    ph.motility.migration_bias = 0;
    ph.motility.migration_speed = t.motility_speed;
    ph.motility.persistence_time = t.persistence_time;
    ph.secretion.set_all_secretion_to_zero();
    ph.secretion.set_all_uptake_to_zero();
    for (const auto &e : t.exchanges) {
      int si = microenvironment.find_density_index(e.substrate);
      ph.secretion.uptake_rates[si] = e.uptake;
      ph.secretion.secretion_rates[si] = e.secretion;
      ph.secretion.saturation_densities[si] = 1;
    }
  }
  // Destroy every cell before definitions/container, including on exceptions.
  struct Cleanup {
    ~Cleanup() {
      while (!all_cells->empty())
        delete_cell(all_cells->back());
    }
  } cleanup;
  auto move = [&](Cell *c, double x, double y) {
    double coords[2] = {x, y};
    const double lo[2] = {cfg.xmin, cfg.ymin}, hi[2] = {cfg.xmax, cfg.ymax};
    for (int j = 0; j < 2; ++j) {
      finite(coords[j], "cell position");
      double len = hi[j] - lo[j];
      double u = std::fmod(coords[j] - lo[j], 2 * len);
      if (u < 0)
        u += 2 * len;
      if (u > len) {
        u = 2 * len - u;
        c->phenotype.motility.motility_vector[j] *= -1;
      }
      coords[j] = std::min(lo[j] + u, std::nextafter(hi[j], lo[j]));
    }
    std::vector<double> pos{coords[0], coords[1], 0};
    int next = container->underlying_mesh.nearest_voxel_index(pos);
    if (next != c->get_current_mechanics_voxel_index()) {
      container->remove_agent(c);
      c->assign_position(pos);
    } else {
      c->position = pos;
      c->update_voxel_index();
    }
    c->is_out_of_domain = false;
    c->is_active = true;
  };
  for (const auto &p : cfg.seeds) {
    membudget::require(kCellBytes);
    auto *c = create_cell(*defs[static_cast<size_t>(p[2])]);
    c->assign_position(p[0], p[1], 0);
    const auto t =
        cfg.types.empty() ? CellType{} : cfg.types[static_cast<size_t>(p[2])];
    c->set_radius(t.radius);
    c->update_motility_vector(t.persistence_time);
  }
  Results out;
  out.counts.cols = 2;
  out.type_counts.cols = 1 + nt;
  for (size_t i = 0; i < nt; ++i)
    out.type_names.push_back(cfg.types.empty() ? "cell" : cfg.types[i].name);
  auto sample = [&](double t) {
    size_t alive = 0;
    std::vector<double> per(nt, 0.0);
    for (auto *c : *all_cells)
      if (!c->phenotype.death.dead) {
        ++alive;
        per[static_cast<size_t>(c->type)] += 1;
      }
    out.counts.values.insert(out.counts.values.end(),
                             {t, static_cast<double>(alive)});
    ++out.counts.rows;
    out.type_counts.values.push_back(t);
    out.type_counts.values.insert(out.type_counts.values.end(), per.begin(),
                                  per.end());
    ++out.type_counts.rows;
  };
  out.history.cols = 5;
  out.profile_history.cols = 3 + cfg.substrates.size();
  auto record = [&](double t) {
    for (auto *c : *all_cells) {
      if (out.history.values.size() > 20000000)
        throw std::runtime_error("history resource limit exceeded; record "
                                 "less often or seed fewer cells");
      out.history.values.insert(out.history.values.end(),
                                {t, c->position[0], c->position[1],
                                 static_cast<double>(c->type),
                                 c->phenotype.death.dead ? 0.0 : 1.0});
      ++out.history.rows;
    }
    for (unsigned v = 0; v < microenvironment.number_of_voxels(); ++v) {
      const auto &p = microenvironment.mesh.voxels[v].center;
      out.profile_history.values.insert(out.profile_history.values.end(),
                                        {t, p[0], p[1]});
      for (size_t i = 0; i < cfg.substrates.size(); ++i)
        out.profile_history.values.push_back(
            microenvironment.density_vector(v)[i]);
      ++out.profile_history.rows;
    }
  };
  size_t next_record = 0;
  auto maybe_record = [&](double t) {
    if (cfg.record_interval <= 0)
      return;
    const double target = next_record * cfg.record_interval;
    if (t + 1e-9 * cfg.record_interval >= target) {
      record(t);
      next_record = static_cast<size_t>(
                        std::floor(t / cfg.record_interval + 1e-9)) +
                    1;
    }
  };
  // Crowding signal and chemotactic gradients use the state at step start.
  const unsigned nvx =
      static_cast<unsigned>(microenvironment.mesh.x_coordinates.size());
  const unsigned nvy =
      static_cast<unsigned>(microenvironment.mesh.y_coordinates.size());
  std::vector<double> crowd(microenvironment.number_of_voxels(), 0.0);
  auto gradient = [&](int v, int si, double g[2]) {
    const auto ijk = microenvironment.mesh.cartesian_indices(v);
    const unsigned i = ijk[0], j = ijk[1];
    auto rho = [&](unsigned a, unsigned b) {
      return microenvironment.density_vector(static_cast<int>(a + b * nvx))[si];
    };
    const double h = cfg.voxel_size;
    g[0] = nvx < 2         ? 0
           : i == 0        ? (rho(1, j) - rho(0, j)) / h
           : i + 1 == nvx  ? (rho(i, j) - rho(i - 1, j)) / h
                           : (rho(i + 1, j) - rho(i - 1, j)) / (2 * h);
    g[1] = nvy < 2         ? 0
           : j == 0        ? (rho(i, 1) - rho(i, 0)) / h
           : j + 1 == nvy  ? (rho(i, j) - rho(i, j - 1)) / h
                           : (rho(i, j + 1) - rho(i, j - 1)) / (2 * h);
  };
  sample(0);
  maybe_record(0);
  double time = 0;
  const auto steps = static_cast<size_t>(steps_d);
  const double fixed_dt =
      steps ? cfg.duration / static_cast<double>(steps) : cfg.dt;
  for (size_t step = 0; step < steps; ++step) {
    const double dt = fixed_dt;
    auto snapshot = *all_cells;
    std::vector<Cell *> remove;
    std::fill(crowd.begin(), crowd.end(), 0.0);
    for (auto *c : snapshot)
      if (!c->phenotype.death.dead)
        crowd[static_cast<size_t>(
            microenvironment.nearest_voxel_index(c->position))] += 1;
    for (auto *c : snapshot) {
      auto &ph = c->phenotype;
      const size_t ti = static_cast<size_t>(c->type);
      const auto t = cfg.types.empty() ? CellType{} : cfg.types[ti];
      double cycle = t.cycle_rate, death = t.death_rate,
             speed = t.motility_speed;
      for (const auto &r : cfg.rules)
        if (r.type == ti) {
          double signal;
          if (r.substrate == kNeighbors) {
            signal = crowd[static_cast<size_t>(
                microenvironment.nearest_voxel_index(c->position))];
            if (!ph.death.dead)
              signal -= 1; // other cells only
          } else
            signal = c->nearest_density_vector()
                [microenvironment.find_density_index(r.substrate)];
          double f = response(signal, r.half_max, r.hill);
          double &v = (r.target == "cycle entry" || r.target == "cycle rate")
                          ? cycle
                      : (r.target == "apoptosis" || r.target == "death rate")
                          ? death
                          : speed;
          if (r.effect == "toward")
            v += (r.saturation - v) * f;
          else
            v *= r.effect == "increases" ? 1 + f : 1 - f;
        }
      if (!std::isfinite(cycle + death + speed) ||
          std::max(cycle, death) * dt > 0.1)
        throw std::runtime_error("rate*dt must not exceed 0.1; reduce dt");
      if (!ph.death.dead) {
        ph.cycle.data.transition_rate(0, 0) = cycle;
        ph.death.rates[ph.death.find_death_model_index(
            PhysiCell_constants::apoptosis_death_model)] = death;
        if (ph.death.check_for_death(dt)) {
          ph.cycle.sync_to_cycle_model(ph.death.current_model());
          ph.motility.is_motile = false;
          ph.secretion.set_all_secretion_to_zero();
          ph.secretion.set_all_uptake_to_zero();
          if (ph.cycle.current_phase().entry_function)
            ph.cycle.current_phase().entry_function(c, ph, dt);
        }
      }
      ph.cycle.advance_cycle(c, ph, dt);
      if (ph.flagged_for_division) {
        if (all_cells->size() >= 100000)
          throw std::runtime_error("cell population limit exceeded");
        ph.flagged_for_division = false;
        membudget::require(kCellBytes);
        auto *child = create_cell(*defs[ti]);
        child->phenotype = ph;
        child->assign_position(c->position);
        child->set_radius(t.radius);
        c->set_radius(t.radius);
        child->update_motility_vector(t.persistence_time);
        // Noninteracting point agents: division creates a colocated daughter;
        // each starts with the configured volume (instantaneous replenishment).
      }
      if (ph.flagged_for_removal) {
        remove.push_back(c);
        continue;
      }
      if (!ph.death.dead && speed > 0) {
        // PhysiCell's unbiased persistent walk, with first-order position
        // stepping.
        const double old = ph.motility.migration_speed;
        ph.motility.migration_speed = speed;
        if (old > 0)
          for (auto &v : ph.motility.motility_vector)
            v *= speed / old;
        if (!t.chemotaxis.empty()) {
          double g[2];
          gradient(microenvironment.nearest_voxel_index(c->position),
                   microenvironment.find_density_index(t.chemotaxis), g);
          const double n = std::hypot(g[0], g[1]);
          auto &d = ph.motility.migration_bias_direction;
          d.assign(3, 0.0);
          if (n > 0) {
            d[0] = g[0] / n;
            d[1] = g[1] / n;
          }
          // No gradient: fall back to the unbiased walk.
          ph.motility.migration_bias = n > 0 ? t.chemotaxis_bias : 0.0;
        }
        c->update_motility_vector(dt);
        move(c, c->position[0] + dt * ph.motility.motility_vector[0],
             c->position[1] + dt * ph.motility.motility_vector[1]);
      }
    }
    for (auto *c : remove)
      delete_cell(c);
    // Exchange and diffusion share a substep, as in PhysiCell's diffusion_dt.
    const size_t sub = static_cast<size_t>(sub_d);
    const double ddt = dt / static_cast<double>(sub);
    for (auto *c : *all_cells)
      c->set_internal_uptake_constants(ddt);
    for (size_t k = 0; k < sub; ++k) {
      for (auto *c : *all_cells)
        c->phenotype.secretion.advance(c, c->phenotype, ddt);
      microenvironment.simulate_diffusion_decay(ddt);
    }

    time = std::min(cfg.duration, (step + 1) * fixed_dt);
    sample(time);
    maybe_record(time);
  }
  out.cells.cols = 4;
  out.cells.rows = all_cells->size();
  for (auto *c : *all_cells)
    out.cells.values.insert(out.cells.values.end(),
                            {c->position[0], c->position[1],
                             static_cast<double>(c->type),
                             c->phenotype.death.dead ? 0.0 : 1.0});
  out.profiles.cols = 2 + cfg.substrates.size();
  out.profiles.rows = microenvironment.number_of_voxels();
  for (const auto &s : cfg.substrates)
    out.substrate_names.push_back(s.name);
  for (unsigned v = 0; v < microenvironment.number_of_voxels(); ++v) {
    const auto &p = microenvironment.mesh.voxels[v].center;
    out.profiles.values.insert(out.profiles.values.end(), {p[0], p[1]});
    for (size_t i = 0; i < cfg.substrates.size(); ++i)
      out.profiles.values.push_back(microenvironment.density_vector(v)[i]);
  }
  return out;
}
} // namespace lmcell
