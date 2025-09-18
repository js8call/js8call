module crc
  use, intrinsic :: iso_c_binding, only: c_int, c_loc, c_int8_t, c_bool, c_short
  interface

    function crc14 (data, length) bind (C, name="crc14")
      use, intrinsic :: iso_c_binding, only: c_short, c_ptr, c_int
      implicit none
      integer (c_short) :: crc14
      type (c_ptr), value :: data
      integer (c_int), value :: length
    end function crc14

    function crc14_check (data, length) bind (C, name="crc16_check")
      use, intrinsic :: iso_c_binding, only: c_bool, c_ptr, c_int
      implicit none
      logical (c_bool) :: crc14_check
      type (c_ptr), value :: data
      integer (c_int), value :: length
    end function crc14_check

    function crc12 (data, length) bind (C, name="crc12")
      use, intrinsic :: iso_c_binding, only: c_short, c_ptr, c_int
      implicit none
      integer (c_short) :: crc12
      type (c_ptr), value :: data
      integer (c_int), value :: length
    end function crc12

    function crc12_check (data, length) bind (C, name="crc12_check")
      use, intrinsic :: iso_c_binding, only: c_bool, c_ptr, c_int
      implicit none
      logical (c_bool) :: crc12_check
      type (c_ptr), value :: data
      integer (c_int), value :: length
    end function crc12_check

    function crc10 (data, length) bind (C, name="crc10")
      use, intrinsic :: iso_c_binding, only: c_short, c_ptr, c_int
      implicit none
      integer (c_short) :: crc10
      type (c_ptr), value :: data
      integer (c_int), value :: length
    end function crc10

    function crc10_check (data, length) bind (C, name="crc10_check")
      use, intrinsic :: iso_c_binding, only: c_bool, c_ptr, c_int
      implicit none
      logical (c_bool) :: crc10_check
      type (c_ptr), value :: data
      integer (c_int), value :: length
    end function crc10_check

  end interface
end module crc
