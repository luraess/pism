/* Copyright (C) 2021 PISM Authors
 *
 * This file is part of PISM.
 *
 * PISM is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 *
 * PISM is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PISM; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef CDIPIOINITIALIZER_H
#define CDIPIOINITIALIZER_H

#include <string>
#include <mpi.h>

namespace pism {
namespace cdipio {

class Initializer {
public:
  Initializer(unsigned int n_writers, const std::string &IOmode, MPI_Comm glob);
  ~Initializer();
  MPI_Comm comp_comm();
  int pio_namespace();
  void activate_namespace();

private:
  MPI_Comm m_comp_comm;
  int m_pioNamespace;
  bool m_initialized;
  int define_mode(const std::string &IOmode);
};

} // namespace cdipio
} // namespace pism

#endif /* CDIPIOINITIALIZER_H */
