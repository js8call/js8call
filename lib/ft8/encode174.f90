subroutine encode174(message,codeword)
! Encode an 87-bit message and return a 174-bit codeword.
! The generator matrix has dimensions (87,87).
! The code is a (174,87) regular ldpc code with column weight 3.
! The code was generated using the PEG algorithm.
! After creating the codeword, the columns are re-ordered according to
! "colorder" to make the codeword compatible with the parity-check matrix
!

include "ldpc_174_87_params.f90"

integer*1 codeword(N)
integer*1 gen(M,K)
integer*1 itmp(N)
integer*1 message(K)
integer*1 pchecks(M)
logical first
data first/.true./

save first,gen

if( first ) then ! fill the generator matrix
  gen=0
  do i=1,M
    do j=1,11
      read(g(i)( (j-1)*2+1:(j-1)*2+2 ),"(Z2)") istr
        do jj=1, 8
          icol=(j-1)*8+jj
          if( icol .le. 87 ) then
            if( btest(istr,8-jj) ) gen(i,icol)=1
          endif
        enddo
    enddo
  enddo
first=.false.
endif

do i=1,M
  nsum=0
  do j=1,K
    nsum=nsum+message(j)*gen(i,j)
  enddo
  pchecks(i)=mod(nsum,2)
enddo
itmp(1:M)=pchecks
itmp(M+1:N)=message(1:K)
codeword(colorder+1)=itmp(1:N)

return
end subroutine encode174
