#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lmcell {

struct Matrix {
  std::size_t rows = 0;
  std::size_t cols = 0;
  std::vector<double> values;
  double at(std::size_t row, std::size_t col) const;
};

struct Substrate {
  std::string name;
  double diffusion = 0.0;
  double decay = 0.0;
  double initial = 0.0;
  double boundary = -1.0;
};

struct CellType {
  std::string name = "cell";
  double cycle_rate = 0.0;
  double death_rate = 0.0;
  double motility_speed = 0.0;
  double persistence_time = 1.0;
  double radius = 8.0;
  std::string substrate;
  double uptake_rate = 0.0;
  double secretion_rate = 0.0;
};

struct Rule {
  std::string substrate;
  std::string effect;
  std::string target;
  double half_max = 1.0;
  double hill = 1.0;
  std::size_t type = 0;
};

struct Config {
  double xmin = -100.0;
  double xmax = 100.0;
  double ymin = -100.0;
  double ymax = 100.0;
  double voxel_size = 20.0;
  double duration = 0.0;
  double dt = 1.0;
  std::uint64_t seed = 1;
  std::vector<Substrate> substrates;
  std::vector<CellType> types;
  std::vector<Rule> rules;
  std::vector<std::vector<double>> seeds;
};

struct Results {
  Matrix counts;   // time, live cells
  Matrix cells;    // final x,y,type,alive
  Matrix profiles; // voxel x,y,substrate concentrations
  std::vector<std::string> substrate_names;
};

// PhysiCell/BioFVM are process-global by design. A process may execute one
// simulation; a second construction fails with a descriptive exception.
Results run(const Config &config);

} // namespace lmcell
