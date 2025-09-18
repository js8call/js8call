subroutine chkcrc12a(decoded,nbadcrc)

  use crc
  integer*1 decoded(87)
  integer*1, target:: i1Dec8BitBytes(11)
  character*87 cbits

! Write decoded bits into cbits: 75-bit message plus 12-bit CRC
  write(cbits,1000) decoded
1000 format(87i1)
  read(cbits,1001) i1Dec8BitBytes
1001 format(11b8)
  read(cbits,1002) ncrc12                         !Received CRC12
1002 format(75x,b12)

  i1Dec8BitBytes(10)=iand(i1Dec8BitBytes(10),transfer(128+64+32,0_1))
  i1Dec8BitBytes(11)=0
  icrc12=crc12(c_loc(i1Dec8BitBytes),11)          !CRC12 computed from 75 msg bits
  icrc12=xor(icrc12, 42) ! TODO: jsherer - could change the crc here

  nbadcrc=1
  if(ncrc12.eq.icrc12) nbadcrc=0

  return
end subroutine chkcrc12a
