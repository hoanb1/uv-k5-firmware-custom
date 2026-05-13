def show_alignment(val, expected):
    v_str = f"~{val:064b}"
    e_str = f" {expected:064b}"
    print(v_str)
    print(e_str)
    
    xor = val ^ expected
    print(f" {xor:064b}")
    print("Errors:", bin(xor).count('1'))

val = ~0xEEF06C77FF8677E9 & 0xFFFFFFFFFFFFFFFF
expected = 0x086D53884469481F
show_alignment(val, expected)
