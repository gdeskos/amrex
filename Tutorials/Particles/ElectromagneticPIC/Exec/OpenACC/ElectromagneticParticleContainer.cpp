#include "ElectromagneticParticleContainer.H"
#include "Constants.H"

#include "em_pic_F.H"

using namespace amrex;

namespace
{    
    void get_position_unit_cell(Real* r, const IntVect& nppc, int i_part)
    {
        int nx = nppc[0];
        int ny = nppc[1];
        int nz = nppc[2];

        int ix_part = i_part/(ny * nz);
        int iy_part = (i_part % (ny * nz)) % ny;
        int iz_part = (i_part % (ny * nz)) / ny;

        r[0] = (0.5+ix_part)/nx;
        r[1] = (0.5+iy_part)/ny;
        r[2] = (0.5+iz_part)/nz;
    }

    void get_gaussian_random_momentum(Real* u, Real u_mean, Real u_std) {
        Real ux_th = amrex::RandomNormal(0.0, u_std);
        Real uy_th = amrex::RandomNormal(0.0, u_std);
        Real uz_th = amrex::RandomNormal(0.0, u_std);

        u[0] = u_mean + ux_th;
        u[1] = u_mean + uy_th;
        u[2] = u_mean + uz_th;
    }
}

ElectromagneticParticleContainer::
ElectromagneticParticleContainer(const Geometry            & a_geom,
                                 const DistributionMapping & a_dmap,
                                 const BoxArray            & a_ba,
                                 const int                   a_species_id,
                                 const Real                  a_charge,
                                 const Real                  a_mass)
    : ParticleContainer<0, 0, PIdx::nattribs, 0>(a_geom, a_dmap, a_ba),
    m_species_id(a_species_id), m_charge(a_charge), m_mass(a_mass)
{}

void
ElectromagneticParticleContainer::
InitParticles(const IntVect& a_num_particles_per_cell,
              const Real     a_thermal_momentum_std,
              const Real     a_thermal_momentum_mean,
              const Real     a_density,
              const RealBox& a_bounds,
              const int      a_problem)
{
    BL_PROFILE("ElectromagneticParticleContainer::InitParticles");
    
    const Real* dx = m_geom.CellSize();
    
    const int num_ppc = AMREX_D_TERM( a_num_particles_per_cell[0],
                                      *a_num_particles_per_cell[1],
                                      *a_num_particles_per_cell[2]);
    const Real scale_fac = dx[0]*dx[1]*dx[2]/num_ppc;
    
    std::array<Real,PIdx::nattribs> attribs;
    attribs.fill(0.0);    
    
    for(MFIter mfi(*m_mask_ptr); mfi.isValid(); ++mfi)
    {
        const Box& tile_box  = mfi.tilebox();
        const Real* plo = m_geom.ProbLo();
        const int grid_id = mfi.index();
        const int tile_id = mfi.LocalTileIndex();
        const auto& pair_index = std::make_pair(grid_id, tile_id);
        auto& particles = m_particles[pair_index];
        for (IntVect iv = tile_box.smallEnd(); iv <= tile_box.bigEnd(); tile_box.next(iv)) {
            for (int i_part=0; i_part<num_ppc;i_part++) {
                Real r[3];
                Real u[3];
                
                get_position_unit_cell(r, a_num_particles_per_cell, i_part);
                
                if (a_problem == 0) {
                    get_gaussian_random_momentum(u, a_thermal_momentum_mean,
                                                 a_thermal_momentum_std);
                }
                else if (a_problem == 1 ) {
                    u[0] = 0.01;
                    u[1] = 0.0;
                    u[2] = 0.0;
                } else {
                    amrex::Abort("problem type not valid");
                }
                
                Real x = plo[0] + (iv[0] + r[0])*dx[0];
                Real y = plo[1] + (iv[1] + r[1])*dx[1];
                Real z = plo[2] + (iv[2] + r[2])*dx[2];
                
                if (x >= a_bounds.hi(0) || x < a_bounds.lo(0) ||
                    y >= a_bounds.hi(1) || y < a_bounds.lo(1) ||
                    z >= a_bounds.hi(2) || z < a_bounds.lo(2) ) continue;
                
                ParticleType p;
                p.id()  = ParticleType::NextID();
                p.cpu() = ParallelDescriptor::MyProc();                
                p.pos(0) = x;
                p.pos(1) = y;
                p.pos(2) = z;
                
                attribs[PIdx::ux] = u[0] * PhysConst::c;
                attribs[PIdx::uy] = u[1] * PhysConst::c;
                attribs[PIdx::uz] = u[2] * PhysConst::c;
                attribs[PIdx::w ] = a_density * scale_fac;
                
                host_particles.push_back(p);
                for (int kk = 0; kk < PIdx::nattribs; ++kk)
                    host_attribs[kk].push_back(attribs[kk]);
                
                attribs[PIdx::ux] = u[0] * PhysConst::c;
                attribs[PIdx::uy] = u[1] * PhysConst::c;
                attribs[PIdx::uz] = u[2] * PhysConst::c;
                attribs[PIdx::w ] = a_density * scale_fac;                                
            }
        }
        
        auto& particle_tile = GetParticles(lev)[std::make_pair(grid_id,tile_id)];
        auto old_size = particle_tile.GetArrayOfStructs().size();
        auto new_size = old_size + host_particles.size();
        particle_tile.resize(new_size);
        
        thrust::copy(host_particles.begin(),
                     host_particles.end(),
                     particle_tile.GetArrayOfStructs().begin() + old_size);
        
        for (int kk = 0; kk < PIdx::nattribs; ++kk)
        {
            thrust::copy(host_attribs[kk].begin(),
                         host_attribs[kk].end(),
                         particle_tile.GetStructOfArrays().GetRealData(kk).begin() + old_size);
        }
    }
}

void ElectromagneticParticleContainer::
PushAndDeposeParticles(const MultiFab& Ex, const MultiFab& Ey, const MultiFab& Ez,
                       const MultiFab& Bx, const MultiFab& By, const MultiFab& Bz,
                       MultiFab& jx, MultiFab& jy, MultiFab& jz, Real dt)
{
    BL_PROFILE("ElectromagneticParticleContainer::PushAndDeposeParticles");
    
    const Real* dx  = m_geom.CellSize();
    const Real* plo = m_geom.ProbLo();

    for (MFIter mfi(*m_mask_ptr, false); mfi.isValid(); ++mfi)
    {
        const int grid_id = mfi.index();
        const int tile_id = mfi.LocalTileIndex();
        auto& particles = m_particles[std::make_pair(grid_id, tile_id)];
        const int np    = particles.numParticles();

        if (np == 0) continue;

	FTOC(gather_magnetic_field)(np,
                                    particles.x().data(),  particles.y().data(),  particles.z().data(),
                                    particles.bx().data(), particles.by().data(), particles.bz().data(),
                                    BL_TO_FORTRAN_3D(Bx[mfi]),
                                    BL_TO_FORTRAN_3D(By[mfi]),
                                    BL_TO_FORTRAN_3D(Bz[mfi]),
                                    plo, dx);

	FTOC(gather_electric_field)(np,
                                    particles.x().data(),  particles.y().data(),  particles.z().data(),
                                    particles.ex().data(), particles.ey().data(), particles.ez().data(),
                                    BL_TO_FORTRAN_3D(Ex[mfi]),
                                    BL_TO_FORTRAN_3D(Ey[mfi]),
                                    BL_TO_FORTRAN_3D(Ez[mfi]),
                                    plo, dx);

        FTOC(push_momentum_boris)(np,
                                  particles.ux().data(), particles.uy().data(), particles.uz().data(),
                                  particles.ginv().data(),
                                  particles.ex().data(), particles.ey().data(), particles.ez().data(),
                                  particles.bx().data(), particles.by().data(), particles.bz().data(),
                                  m_charge, m_mass, dt);
        
        FTOC(push_position_boris)(np,
                                  particles.x().data(),  particles.y().data(),  particles.z().data(),
                                  particles.ux().data(), particles.uy().data(), particles.uz().data(),
                                  particles.ginv().data(), dt);
        
        FTOC(deposit_current)(BL_TO_FORTRAN_3D(jx[mfi]),
                              BL_TO_FORTRAN_3D(jy[mfi]),
                              BL_TO_FORTRAN_3D(jz[mfi]),
                              np,
                              particles.x().data(),  particles.y().data(),  particles.z().data(),
                              particles.ux().data(), particles.uy().data(), particles.uz().data(),
                              particles.ginv().data(), particles.w().data(),
                              m_charge, plo, dt, dx);
    }
}

void ElectromagneticParticleContainer::
PushParticleMomenta(const MultiFab& Ex, const MultiFab& Ey, const MultiFab& Ez,
                    const MultiFab& Bx, const MultiFab& By, const MultiFab& Bz, Real dt)
{   
    BL_PROFILE("ElectromagneticParticleContainer::PushParticleMomenta");
    
    const Real* dx  = m_geom.CellSize();
    const Real* plo = m_geom.ProbLo();

    for (MFIter mfi(*m_mask_ptr, false); mfi.isValid(); ++mfi)
    {
        const int grid_id = mfi.index();
        const int tile_id = mfi.LocalTileIndex();
        auto& particles = m_particles[std::make_pair(grid_id, tile_id)];
        const int np    = particles.numParticles();

        if (np == 0) continue;

	FTOC(gather_magnetic_field)(np,
                                    particles.x().data(),  particles.y().data(),  particles.z().data(),
                                    particles.bx().data(), particles.by().data(), particles.bz().data(),
                                    BL_TO_FORTRAN_3D(Bx[mfi]),
                                    BL_TO_FORTRAN_3D(By[mfi]),
                                    BL_TO_FORTRAN_3D(Bz[mfi]),
                                    plo, dx);

	FTOC(gather_electric_field)(np,
                                    particles.x().data(),  particles.y().data(),  particles.z().data(),
                                    particles.ex().data(), particles.ey().data(), particles.ez().data(),
                                    BL_TO_FORTRAN_3D(Ex[mfi]),
                                    BL_TO_FORTRAN_3D(Ey[mfi]),
                                    BL_TO_FORTRAN_3D(Ez[mfi]),
                                    plo, dx);

        FTOC(push_momentum_boris)(np,
                                  particles.ux().data(), particles.uy().data(), particles.uz().data(),
                                  particles.ginv().data(),
                                  particles.ex().data(), particles.ey().data(), particles.ez().data(),
                                  particles.bx().data(), particles.by().data(), particles.bz().data(),
                                  m_charge, m_mass, dt);
    }
}

void ElectromagneticParticleContainer::PushParticlePositions(Real dt)
{
    BL_PROFILE("ElectromagneticParticleContainer::PushParticlePositions");
    
    for (MFIter mfi(*m_mask_ptr, false); mfi.isValid(); ++mfi)
    {
        const int grid_id = mfi.index();
        const int tile_id = mfi.index();
        auto& particles = m_particles[std::make_pair(grid_id, tile_id)];
        const int np    = particles.numParticles();

        if (np == 0) continue;

        FTOC(set_gamma)(np,
                        particles.ux().data(), particles.uy().data(), particles.uz().data(),
                        particles.ginv().data());
        
        FTOC(push_position_boris)(np,
                                  particles.x().data(),  particles.y().data(),  particles.z().data(),
                                  particles.ux().data(), particles.uy().data(), particles.uz().data(),
                                  particles.ginv().data(), dt);
    }
}