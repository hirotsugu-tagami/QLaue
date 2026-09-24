// Regression checks for matrix findings from docs/AUDIT-2026-09-24.md.
#include <cmath>
#include <cstring>
#include <iostream>
#include "matrix.h"

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    const char *probe = argv[1];
    if (!std::strcmp(probe, "inverse")) {
        Matrix matrix(1, 2, 3, 0, 1, 4, 5, 6, 0);
        Matrix inverse = matrix.inv();
        std::cout << inverse;
        Matrix identity = matrix * inverse;
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                if (std::fabs(identity.Get(row, col) - (row == col ? 1 : 0)) > 1e-12)
                    return 1;
    } else if (!std::strcmp(probe, "determinant")) {
        Matrix matrix(1, 2, 3, 0, 1, 4, 5, 6, 0);
        std::cout << "determinant=" << matrix.det() << "; expected=1" << std::endl;
        return std::fabs(matrix.det() - 1) < 1e-12 ? 0 : 1;
    } else if (!std::strcmp(probe, "assignment")) {
        Matrix small(1, 1), large(3, 3);
        large.identity();
        small = large;
        if (small.Rows() != 3 || small.Cols() != 3 || small.Get(2, 2) != 1)
            return 1;
        Matrix empty;
        empty = large;
        if (empty.Get(2, 2) != 1)
            return 1;
    } else if (!std::strcmp(probe, "transpose")) {
        Matrix column(1.0, 2.0, 3.0);
        Matrix row = column.transpose();
        std::cout << row;
        if (row.Rows() != 1 || row.Cols() != 3 || row.Get(0, 2) != 3)
            return 1;
    } else if (!std::strcmp(probe, "rectangular")) {
        Matrix a(2, 3);
        for (int i = 0; i < 6; ++i)
            a._put(i, i + 1);
        Matrix b = (a * 3 - a) / 2 + a;
        for (int i = 0; i < 6; ++i)
            if (b._get(i) != 2 * (i + 1))
                return 1;
    } else if (!std::strcmp(probe, "destruction")) {
        Matrix matrix(3, 3);
        matrix.identity();
    } else {
        return 2;
    }
    return 0;
}
