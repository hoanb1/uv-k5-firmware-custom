def gf_mul(x, y):
    z = 0
    for i in range(8):
        if (y >> i) & 1: z ^= x
        x = (x << 1) ^ 0x11D if x & 0x80 else x << 1
    return z

def gf_poly_mul(p, q):
    r = [0] * (len(p) + len(q) - 1)
    for i in range(len(p)):
        for j in range(len(q)):
            r[i+j] ^= gf_mul(p[i], q[j])
    return r

g = [1]
root = 1
for i in range(10):
    g = gf_poly_mul(g, [1, root])
    root = gf_mul(root, 2)
print("gen:", g)
