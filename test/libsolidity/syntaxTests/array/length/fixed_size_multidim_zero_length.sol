contract C {
  function a() public pure returns(int[0][500] memory) {}
  function b() public pure returns(uint[0][500] memory) {}
  function c() public pure returns(byte[0][500] memory) {}
  function d() public pure returns(bytes32[0][500] memory) {}
  function e() public pure returns(bytes[0][500] memory) {}
  function e() public pure returns(string[0][500] memory) {}
}
// ----
// TypeError 1406: (52-53): Array with zero length specified.
// TypeError 1406: (111-112): Array with zero length specified.
// TypeError 1406: (170-171): Array with zero length specified.
// TypeError 1406: (232-233): Array with zero length specified.
// TypeError 1406: (292-293): Array with zero length specified.
// TypeError 1406: (353-354): Array with zero length specified.
