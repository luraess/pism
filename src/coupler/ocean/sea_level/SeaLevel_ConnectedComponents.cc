#include "SeaLevel_ConnectedComponents.hh"

namespace pism{

SeaLevelCC::SeaLevelCC(IceGrid::ConstPtr g,
                       const double drho,
                       const IceModelVec2S &bed,
                       const IceModelVec2S &thk,
                       const double fill_value)
  :FillingAlgCC<SinkCC>(g, drho, bed, thk, fill_value) {

  // prepare the mask
  {
    IceModelVec::AccessList list{ &m_mask_run };
    for (Points p(*m_grid); p; p.next()) {
      const int i = p.i(), j = p.j();

      // Set "sink" at a margin of the computational domain
      m_mask_run(i, j) = grid_edge(*m_grid, i, j) ? 1 : 0;
    }
    m_mask_run.update_ghosts();
  }
}

SeaLevelCC::SeaLevelCC(IceGrid::ConstPtr g,
                       const double drho,
                       const IceModelVec2S &bed,
                       const IceModelVec2S &thk,
                       const IceModelVec2Int &run_mask,
                       const double fill_value)
  :FillingAlgCC<SinkCC>(g, drho, bed, thk, fill_value) {
  m_mask_run.copy_from(run_mask);
}


static void label(int run_number, const VecList lists, IceModelVec2S &result, double value) {

  IceModelVec::AccessList list{&result};

  const auto
    &i_vec   = lists.find("i")->second,
    &j_vec   = lists.find("j")->second,
    &len_vec = lists.find("lengths")->second,
    &parents = lists.find("parents")->second;

  for(int k = 0; k <= run_number; ++k) {
    const int label = trackParentRun(k, parents);
    if(label > 1) {
      auto j = static_cast<int>(j_vec[k]);
      for(int n = 0; n < len_vec[k]; ++n) {
        auto i = static_cast<int>(i_vec[k]) + n;
        result(i, j) = value;
      }
    }
  }
}

void SeaLevelCC::computeMask(const IceModelVec2S &SeaLevel, const double Offset, IceModelVec2Int &result) {
  m_sea_level = &SeaLevel;
  m_offset = Offset;

  VecList lists;
  unsigned int max_items = 2 * m_grid->ym();
  init_VecList(lists, max_items);

  int run_number = 1;

  IceModelVec::AccessList list{ m_sea_level };
  compute_runs(run_number, lists, max_items);

  // Initialize the mask:
  result.set(0.0);
  label(run_number, lists, result, 1);
}

bool SeaLevelCC::ForegroundCond(int i, int j) const {
  double bed = (*m_bed)(i, j),
         thk = (*m_thk)(i, j),
         sea_level = (*m_sea_level)(i, j);
  int mask = m_mask_run.as_int(i, j);

  return is_foreground(bed, thk, mask, sea_level, m_offset);
}

}
