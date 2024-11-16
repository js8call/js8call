  use, intrinsic :: iso_c_binding, only: c_int, c_short, c_float, c_char, c_bool

  include 'constants.f90'

  !
  ! these structures must be kept in sync with ../commons.h
  !
  type, bind(C) :: params_block
     integer(c_int) :: nutc
     logical(c_bool) :: ndiskdat
     integer(c_int) :: nfqso
     logical(c_bool) :: newdat
     integer(c_int) :: npts8
     integer(c_int) :: nfa
     integer(c_int) :: nfb
     logical(c_bool) :: syncStats
     integer(c_int) :: kin
     integer(c_int) :: kposA
     integer(c_int) :: kposB
     integer(c_int) :: kposC
     integer(c_int) :: kposE
     integer(c_int) :: kposI
     integer(c_int) :: kszA
     integer(c_int) :: kszB
     integer(c_int) :: kszC
     integer(c_int) :: kszE
     integer(c_int) :: kszI
     integer(c_int) :: nsubmode
     integer(c_int) :: nsubmodes
     logical(c_bool) :: nagain
     integer(c_int) :: ndepth
     integer(c_int) :: napwid
     integer(c_int) :: nmode
     integer(c_int) :: nranera
     character(kind=c_char) :: datetime(20)
     character(kind=c_char) :: mycall(12)
  end type params_block

  type, bind(C) :: dec_data
     real(c_float) :: ss(184,NSMAX)
     real(c_float) :: savg(NSMAX)
     integer(c_short) :: id2(NMAX)
     type(params_block) :: params
  end type dec_data
