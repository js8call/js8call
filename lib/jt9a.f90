subroutine jt9a()
  use, intrinsic :: iso_c_binding, only: c_f_pointer
  use prog_args
  use timer_module, only: timer
  use timer_impl, only: init_timer !, limtrace

  include 'jt9com.f90'

! These routines connect the shared memory region to the decoder.
  interface
     function address_jt9()
       use, intrinsic :: iso_c_binding, only: c_ptr
       type(c_ptr) :: address_jt9
     end function address_jt9
  end interface

  integer*1 attach_jt9
!  integer*1 lock_jt9,unlock_jt9
  integer size_jt9
! Multiple instances:
  character*80 mykey
  type(dec_data), pointer :: shared_data
  type(params_block) :: local_params
  logical fileExists

! Multiple instances:
  i0 = len(trim(shm_key))

  call init_timer (trim(data_dir)//'/timer.out')
!  open(23,file=trim(data_dir)//'/CALL3.TXT',status='unknown')

!  limtrace=-1                            !Disable all calls to timer()

! Multiple instances: set the shared memory key before attaching
  mykey=trim(repeat(shm_key,1))
  i0 = len(mykey)
  i0=setkey_jt9(trim(mykey))
  i1=attach_jt9()
  msdelay=10

10 inquire(file=trim(temp_dir)//'/.lock',exist=fileExists)
  if(fileExists) then
     call sleep_msec(msdelay)
     go to 10
  endif

  inquire(file=trim(temp_dir)//'/.quit',exist=fileExists)
  if(fileExists) then
     i1=detach_jt9()
     go to 999
  endif
  if(i1.eq.999999) stop                  !Silence compiler warning

  nbytes=size_jt9()
  if(nbytes.le.0) then
     print*,'jt9a: Shared memory mem_jt9 does not exist.'
     print*,"Must start 'jt9 -s <thekey>' from within WSJT-X."
     go to 999
  endif
  call c_f_pointer(address_jt9(),shared_data)
  local_params=shared_data%params !save a copy because wsjtx carries on accessing
  call flush(6)
  call timer('decoder ',0)
  call multimode_decoder(shared_data%id2,local_params)
  call timer('decoder ',1)
  call sleep_msec(100)
  go to 10

999 call timer('decoder ',101)

  return
end subroutine jt9a
