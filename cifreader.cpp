// CIF 1.1 structure import. See https://www.iucr.org/resources/cif/spec/version1.1/cifsyntax
// SPDX-License-Identifier: GPL-2.0-or-later
#include "cifreader.h"
#include "crystal.h"
#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <cmath>

extern spacegroup spacegroups[];

namespace {
struct CifError {
    QString message;
    int line;
};

void fail(const QString &message, int line = 0) {
    throw CifError{message, line};
}

struct Token {
    QString text;
    int line;
    bool quoted;
    bool isTag() const { return !quoted && text.startsWith('_'); }
    bool control() const {
        const QString lower = text.toLower();
        return !quoted && (isTag() || lower.startsWith("data_") ||
            lower.startsWith("save_") || lower == "loop_" ||
            lower == "stop_" || lower == "global_");
    }
};

QVector<Token> tokenize(QString text) {
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    if(text.startsWith(QChar(0xfeff))) text.remove(0, 1);
    if(text.startsWith("#\\#CIF_2.0"))
        fail("CIF 2.0 is not supported. Export a CIF 1.1 structure instead.");
    QVector<Token> tokens;
    int pos = 0, line = 1;
    while(pos < text.size()) {
        const QChar c = text.at(pos);
        if(c.isSpace()) {
            if(c == '\n') ++line;
            ++pos;
            continue;
        }
        if(c == '#') {
            while(pos < text.size() && text.at(pos) != '\n') ++pos;
            continue;
        }
        Token token{QString(), line, false};
        if(c == ';' && (pos == 0 || text.at(pos-1) == '\n')) {
            token.quoted = true;
            const int start = ++pos;
            while(pos < text.size() && !(text.at(pos) == ';' && text.at(pos-1) == '\n')) {
                if(text.at(pos) == '\n') ++line;
                ++pos;
            }
            if(pos == text.size()) fail("Unterminated multiline text.", token.line);
            token.text = text.mid(start, pos-start).trimmed();
            ++pos;
            if(pos < text.size() && !text.at(pos).isSpace())
                fail("Expected whitespace after multiline text.", line);
        } else if(c == '\'' || c == '"') {
            token.quoted = true;
            const int start = ++pos;
            while(pos < text.size()) {
                if(text.at(pos) == '\n') fail("Unterminated quoted value.", token.line);
                if(text.at(pos) == c && (pos+1 == text.size() || text.at(pos+1).isSpace())) break;
                ++pos;
            }
            if(pos == text.size()) fail("Unterminated quoted value.", token.line);
            token.text = text.mid(start, pos-start);
            ++pos;
        } else {
            const int start = pos;
            while(pos < text.size() && !text.at(pos).isSpace()) ++pos;
            token.text = text.mid(start, pos-start);
        }
        tokens.append(token);
    }
    return tokens;
}

struct Block {
    QString name;
    QMap<QString, QStringList> values;
};

QString tagName(const Token &token) {
    QString name = token.text.toLower();
    return name.replace('.', '_');
}

QVector<Block> parse(const QVector<Token> &tokens) {
    QVector<Block> blocks;
    QSet<QString> names;
    int pos = 0;
    while(pos < tokens.size()) {
        const Token &token = tokens.at(pos++);
        const QString lower = token.text.toLower();
        if(!token.quoted && lower.startsWith("data_")) {
            if(lower.size() == 5 || names.contains(lower))
                fail("Missing or duplicate data block name.", token.line);
            names.insert(lower);
            blocks.append(Block{token.text.mid(5), QMap<QString, QStringList>()});
            continue;
        }
        if(blocks.isEmpty()) fail("Expected a data_ block header.", token.line);
        Block &block = blocks.last();
        QStringList tags;
        const bool loop = !token.quoted && lower == "loop_";
        if(loop) {
            while(pos < tokens.size() && tokens.at(pos).isTag())
                tags.append(tagName(tokens.at(pos++)));
            if(tags.isEmpty()) fail("A loop has no column names.", token.line);
        } else if(token.isTag()) {
            tags.append(tagName(token));
        } else {
            fail("Unexpected token: " + token.text, token.line);
        }
        for(const QString &tag : tags) {
            if(block.values.contains(tag)) fail("Duplicate tag: " + tag, token.line);
            block.values.insert(tag, QStringList());
        }
        int count = 0;
        while(pos < tokens.size() && !tokens.at(pos).control()) {
            block.values[tags.at(count % tags.size())].append(tokens.at(pos++).text);
            ++count;
            if(!loop) break;
        }
        if(count == 0 || count % tags.size() != 0)
            fail("Missing value or incomplete loop row.", token.line);
    }
    return blocks;
}

QString value(const Block &block, const QStringList &tags) {
    for(const QString &tag : tags) {
        const QStringList values = block.values.value(tag);
        if(values.size() > 1) fail("Expected a single value for " + tag);
        if(values.size() == 1 && values.first() != "." && values.first() != "?")
            return values.first();
    }
    return QString();
}

double number(QString text, const QString &field) {
    // Keep the central value of e.g. 5.431(2), including exponent notation.
    static const QRegularExpression syntax(
        "^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)"
        "(?:(?:[eE][+-]?[0-9]+)?(?:\\([0-9]+\\))?|\\([0-9]+\\)[eE][+-]?[0-9]+)$");
    if(!syntax.match(text).hasMatch()) fail("Missing or invalid number for " + field + ": " + text);
    text.remove(QRegularExpression("\\([0-9]+\\)"));
    bool ok = false;
    const double result = text.toDouble(&ok);
    if(!ok || !std::isfinite(result)) fail("Invalid number for " + field + ": " + text);
    return result;
}

QString normalized(QString text) {
    return text.remove(QRegularExpression("[\\s_]")).toLower();
}

QString operationKey(const int *rotation, const int *translation) {
    QStringList parts;
    for(int i=0; i<9; ++i) parts.append(QString::number(rotation[i]));
    for(int i=0; i<3; ++i) parts.append(QString::number((translation[i] % 12 + 12) % 12));
    return parts.join(',');
}

QString operationKey(QString operation) {
    operation = operation.toLower().remove(QRegularExpression("\\s"));
    const QStringList coordinates = operation.split(',');
    if(coordinates.size() != 3) fail("Invalid symmetry operation: " + operation);
    int rotation[9] = {}, translation[3] = {};
    const QRegularExpression term("([+-]?)([xyz]|[0-9]+(?:/[0-9]+|\\.[0-9]+)?)");
    for(int row=0; row<3; ++row) {
        const QString coordinate = coordinates.at(row);
        int pos = 0;
        if(coordinate.isEmpty()) fail("Invalid symmetry operation: " + operation);
        while(pos < coordinate.size()) {
            const QRegularExpressionMatch match = term.match(coordinate, pos);
            if(!match.hasMatch() || match.capturedStart() != pos ||
               (pos > 0 && match.captured(1).isEmpty()))
                fail("Unsupported symmetry operation: " + operation);
            const int sign = match.captured(1) == "-" ? -1 : 1;
            const QString part = match.captured(2);
            const int axis = QString("xyz").indexOf(part);
            if(axis >= 0) {
                rotation[row*3 + axis] += sign;
            } else {
                const QStringList fraction = part.split('/');
                double shift = number(fraction.first(), "symmetry translation");
                if(fraction.size() == 2) shift /= number(fraction.last(), "symmetry denominator");
                const double scaled = 12 * shift;
                if(!std::isfinite(scaled) || std::abs(scaled) > 12000 ||
                   std::abs(scaled - std::round(scaled)) > 1e-6)
                    fail("Unsupported symmetry translation: " + operation);
                translation[row] += sign * int(std::round(scaled));
            }
            pos = match.capturedEnd();
        }
    }
    return operationKey(rotation, translation);
}

QSet<QString> groupOperations(const spacegroup &group) {
    QVector<QVector<int> > shifts;
    shifts.append(QVector<int>{0,0,0});
    switch(group.LatticeType) {
        case 'A': shifts.append(QVector<int>{0,6,6}); break;
        case 'B': shifts.append(QVector<int>{6,0,6}); break;
        case 'C': shifts.append(QVector<int>{6,6,0}); break;
        case 'I': shifts.append(QVector<int>{6,6,6}); break;
        case 'F': shifts.append(QVector<int>{0,6,6});
                  shifts.append(QVector<int>{6,0,6});
                  shifts.append(QVector<int>{6,6,0}); break;
        case 'R': shifts.append(QVector<int>{8,4,4});
                  shifts.append(QVector<int>{4,8,8}); break;
    }
    QSet<QString> operations;
    for(int n=0; n<group.ngenerators; ++n) {
        for(const QVector<int> &shift : shifts) {
            int translation[3];
            for(int i=0; i<3; ++i) translation[i] = group.matrix[n][9+i] + shift.at(i);
            operations.insert(operationKey(group.matrix[n], translation));
        }
    }
    return operations;
}

bool matchesCell(const spacegroup &group, const double *cell) {
    // Cell metrics distinguish, for example, hexagonal and rhombohedral settings.
    double metric[3][3] = {
        {cell[0]*cell[0], cell[0]*cell[1]*cos(cell[5]), cell[0]*cell[2]*cos(cell[4])},
        {cell[0]*cell[1]*cos(cell[5]), cell[1]*cell[1], cell[1]*cell[2]*cos(cell[3])},
        {cell[0]*cell[2]*cos(cell[4]), cell[1]*cell[2]*cos(cell[3]), cell[2]*cell[2]}
    };
    for(int n=0; n<group.ngenerators; ++n) {
        for(int i=0; i<3; ++i) for(int j=0; j<3; ++j) {
            double transformed = 0;
            for(int k=0; k<3; ++k) for(int l=0; l<3; ++l)
                transformed += group.matrix[n][k*3+i] * metric[k][l] * group.matrix[n][l*3+j];
            if(std::abs(transformed-metric[i][j]) > 1e-3 * cell[i] * cell[j]) return false;
        }
    }
    return true;
}

int spaceGroup(const Block &block, const double *cell) {
    const QString hall = value(block, {"_space_group_name_hall", "_symmetry_space_group_name_hall"});
    const QString hm = value(block, {"_space_group_name_h-m_alt", "_symmetry_space_group_name_h-m",
                                     "_space_group_name_h-m_ref", "_space_group_name_h-m_full"});
    const QString it = value(block, {"_space_group_it_number", "_symmetry_int_tables_number"});
    int groupNumber = 0;
    if(!it.isEmpty()) {
        const double n = number(it, "space group number");
        if(n < 1 || n > 230 || n != std::floor(n)) fail("Space group number must be between 1 and 230.");
        groupNumber = int(n);
    }
    QSet<QString> operations;
    QStringList xyz = block.values.value("_space_group_symop_operation_xyz");
    if(xyz.isEmpty()) xyz = block.values.value("_symmetry_equiv_pos_as_xyz");
    for(const QString &operation : xyz) operations.insert(operationKey(operation));
    if(hall.isEmpty() && hm.isEmpty() && !groupNumber && operations.isEmpty())
        fail("No space group was specified. Include a Hall/Hermann-Mauguin symbol, IT number or symmetry operations.");

    for(int i=0; spacegroups[i].number != 0; ++i) {
        const spacegroup &group = spacegroups[i];
        if(groupNumber && group.number != groupNumber) continue;
        if(!hall.isEmpty()) {
            if(normalized(hall) != normalized(group.HallName)) continue;
        } else if(!hm.isEmpty()) {
            bool matches = false;
            for(const QString &alias : QString(group.Name).split('=')) {
                QString expected = normalized(alias);
                if(hm.contains(':')) expected += ":" + normalized(group.Extn);
                if(normalized(hm) == expected) matches = true;
            }
            if(!matches) continue;
        }
        if(!operations.isEmpty() && operations != groupOperations(group)) continue;
        if(!matchesCell(group, cell)) continue;
        return i;
    }
    fail("The space group or its setting is not supported, or the symmetry and cell parameters are inconsistent.");
    return -1;
}

int elementNumber(const QString &text) {
    const QRegularExpressionMatch match = QRegularExpression("^([A-Za-z]{1,2})(?:[0-9_+\\-].*)?$").match(text);
    const QString symbol = match.captured(1);
    if(symbol == "D" || symbol == "T") return 1;
    for(int z=1; z<=98; ++z)
        if(symbol.compare(QString(ZtoName[z-1]).trimmed(), Qt::CaseInsensitive) == 0) return z;
    fail("Unsupported element or atom label: " + text);
    return 0;
}

Crystal structure(const Block &block) {
    const char *tags[] = {"_cell_length_a", "_cell_length_b", "_cell_length_c",
                         "_cell_angle_alpha", "_cell_angle_beta", "_cell_angle_gamma"};
    double cell[6];
    for(int i=0; i<6; ++i) cell[i] = number(value(block, {tags[i]}), tags[i]);
    for(int i=3; i<6; ++i) cell[i] *= M_PI / 180;
    if(!Crystal::isValidLattice(cell[0],cell[1],cell[2],cell[3],cell[4],cell[5]))
        fail("The unit cell has invalid lengths, angles or volume.");
    Crystal crystal;
    crystal.setLattice(cell[0],cell[1],cell[2],cell[3],cell[4],cell[5]);
    crystal.setSpaceGroup(spaceGroup(block, cell));

    const QStringList xs = block.values.value("_atom_site_fract_x");
    const QStringList ys = block.values.value("_atom_site_fract_y");
    const QStringList zs = block.values.value("_atom_site_fract_z");
    QStringList elements = block.values.value("_atom_site_type_symbol");
    if(elements.isEmpty()) elements = block.values.value("_atom_site_label");
    const QStringList occupancies = block.values.value("_atom_site_occupancy");
    if(xs.size() != ys.size() || xs.size() != zs.size() || xs.size() != elements.size() ||
       (!occupancies.isEmpty() && xs.size() != occupancies.size()))
        fail("The atom table needs an element/label and fractional x, y and z for every site.");
    for(int i=0; i<xs.size(); ++i) {
        if(!occupancies.isEmpty()) {
            const double occupancy = number(occupancies.at(i), "atom occupancy");
            if(occupancy == 0) continue;
            if(std::abs(occupancy-1) > 1e-8)
                fail("Partial atom occupancies are not supported by QLaue (site " + QString::number(i+1) + ").");
        }
        if(crystal.getNRootAtoms() >= crystal.maxRootAtoms())
            fail("Too many atom sites for this space group. QLaue reserves at most 2048 symmetry-expanded atoms.");
        double x = number(xs.at(i), "fractional x"), y = number(ys.at(i), "fractional y"), z = number(zs.at(i), "fractional z");
        crystal.addAtom(elementNumber(elements.at(i)), x-std::floor(x), y-std::floor(y), z-std::floor(z));
    }
    QString name = value(block, {"_chemical_name_common", "_chemical_name_mineral", "_chemical_name_systematic", "_chemical_formula_sum"});
    if(name.isEmpty()) name = block.name;
    crystal.setName(name.toUtf8().constData());
    crystal.spaceGroupGenerate();
    return crystal;
}
} // namespace

bool readCifFile(const QString &filename, Crystal &crystal, QString &error, int &errorLine) {
    error.clear();
    errorLine = 0;
    QFile file(filename);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error = file.errorString();
        return false;
    }
    if(file.size() > 16 * 1024 * 1024) {
        error = "CIF files larger than 16 MiB are not supported.";
        return false;
    }
    const QByteArray bytes = file.readAll();
    if(file.error() != QFile::NoError) {
        error = file.errorString();
        return false;
    }
    try {
        const QVector<Block> blocks = parse(tokenize(QString::fromUtf8(bytes)));
        const Block *selected = NULL;
        for(const Block &block : blocks) {
            if(!block.values.contains("_cell_length_a")) continue;
            if(selected) fail("This CIF contains multiple structures. Export one data block and import it separately.");
            selected = &block;
        }
        if(!selected) fail("No crystal structure with unit-cell parameters was found.");
        const Crystal imported = structure(*selected);
        crystal = imported;
        return true;
    } catch(const CifError &failure) {
        errorLine = failure.line;
        error = failure.message;
        if(errorLine) error = QString("Line %1: %2").arg(errorLine).arg(error);
        return false;
    }
}
