/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LATTE_TESTS_SETTINGSSOURCESCANNER_H
#define LATTE_TESTS_SETTINGSSOURCESCANNER_H

// Syntax scanner for SC-F2 (the source-to-ledger coverage gate). This is not a
// general QML or C++ parser. It recognizes the deliberately small source
// grammar that can declare a settings affordance, while retaining enough
// balanced syntax to avoid treating braces in comments, strings, JavaScript,
// or C++ lambdas as declarations.
//
// Invariants:
//   * tokens never contain whitespace or comments, so formatting and comments
//     cannot change a normalized digest;
//   * every delimiter pair is balanced before discovery starts;
//   * a QML object is an UpperCase qualified name immediately before a block;
//   * direct members are read only at the object's own delimiter depth;
//   * candidate selectors must be unique within one file;
//   * source lines are diagnostics only and never enter a selector.
//
// Full-node SHA-256 is intentionally the fallback, not the first choice. An id
// selector survives edits to the node body; a digest selector fails loudly when
// an otherwise anonymous node changes. The ancestry prefix distinguishes
// identical anonymous nodes under different parents. Identical anonymous
// siblings remain ambiguous by design and require a QML id rather than an
// unstable occurrence number.
//
// The scanner is linear apart from the small QML ancestry pass. That pass is
// O(objects^2), which avoids a second parser stack and is bounded by settings
// files containing tens of objects, not application-scale translation units.

#include <QByteArray>
#include <QCryptographicHash>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QVector>

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace Latte::SettingsSource
{

struct Token
{
    enum class Kind
    {
        Identifier,
        Number,
        String,
        Symbol,
    };

    Kind kind{Kind::Symbol};
    QString text;
    int line{1};
    qsizetype offset{0};
    qsizetype endOffset{0};
};

struct SourceSite
{
    QString selector;
    QString category;
    int line{1};

    friend bool operator==(const SourceSite &, const SourceSite &) = default;
};

struct ScanResult
{
    QList<SourceSite> candidates;
    QHash<QString, QList<int>> resolvableLines;
    QStringList errors;

    [[nodiscard]] int resolveCount(const QString &selector) const
    {
        return resolvableLines.value(selector).size();
    }

    [[nodiscard]] QSet<QString> candidateSelectors() const
    {
        QSet<QString> selectors;
        for (const SourceSite &candidate : candidates)
        {
            selectors.insert(candidate.selector);
        }
        return selectors;
    }
};

namespace Detail
{

struct LexResult
{
    QVector<Token> tokens;
    QList<SourceSite> comments;
    QStringList errors;
};

inline bool isIdentifierStart(const QChar character)
{
    return character == QLatin1Char('_') || character == QLatin1Char('$') || character.isLetter();
}

inline bool isIdentifierPart(const QChar character)
{
    return isIdentifierStart(character) || character.isDigit();
}

inline QString sha256(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

inline QString normalizedTokens(const QVector<Token> &tokens, const int begin, const int end)
{
    QStringList normalized;
    normalized.reserve(std::max(0, end - begin));
    for (int index = begin; index < end; ++index)
    {
        normalized.append(tokens.at(index).text);
    }
    return normalized.join(QChar(0x1f));
}

inline QString tokenDigest(const QVector<Token> &tokens, const int begin, const int end)
{
    return sha256(normalizedTokens(tokens, begin, end).toUtf8());
}

inline LexResult lex(const QString &source)
{
    LexResult result;
    const QStringView sourceView{source};
    qsizetype offset = 0;
    int line = 1;

    const auto appendToken = [&](const Token::Kind kind, const qsizetype begin, const qsizetype end, const int tokenLine)
    { result.tokens.append(Token{kind, source.sliced(begin, end - begin), tokenLine, begin, end}); };

    while (offset < source.size())
    {
        const QChar character = source.at(offset);
        if (character.isSpace())
        {
            if (character == QLatin1Char('\n'))
            {
                ++line;
            }
            ++offset;
            continue;
        }

        if (sourceView.sliced(offset).startsWith(QLatin1String("//")))
        {
            const qsizetype begin = offset;
            const int commentLine = line;
            offset += 2;
            while (offset < source.size() && source.at(offset) != QLatin1Char('\n'))
            {
                ++offset;
            }
            const QString comment = source.sliced(begin, offset - begin);
            result.comments.append(
                {QStringLiteral("comment|sha256=%1").arg(sha256(comment.toUtf8())), QStringLiteral("comment"), commentLine});
            continue;
        }

        if (sourceView.sliced(offset).startsWith(QLatin1String("/*")))
        {
            const qsizetype begin = offset;
            const int commentLine = line;
            offset += 2;
            bool closed = false;
            while (offset < source.size())
            {
                if (sourceView.sliced(offset).startsWith(QLatin1String("*/")))
                {
                    offset += 2;
                    closed = true;
                    break;
                }
                if (source.at(offset) == QLatin1Char('\n'))
                {
                    ++line;
                }
                ++offset;
            }
            if (!closed)
            {
                result.errors.append(QStringLiteral("line %1: unterminated block comment").arg(commentLine));
                break;
            }
            const QString comment = source.sliced(begin, offset - begin);
            result.comments.append(
                {QStringLiteral("comment|sha256=%1").arg(sha256(comment.toUtf8())), QStringLiteral("comment"), commentLine});
            continue;
        }

        // C++ raw strings are consumed as one token, including every standard
        // encoding prefix. The delimiter follows the C++ 16-character limit
        // and excludes whitespace, parentheses, and backslashes.
        QString rawPrefix;
        for (const QString &prefix : {QStringLiteral("u8R\""), QStringLiteral("uR\""), QStringLiteral("UR\""),
                                      QStringLiteral("LR\""), QStringLiteral("R\"")})
        {
            if (sourceView.sliced(offset).startsWith(prefix))
            {
                rawPrefix = prefix;
                break;
            }
        }
        if (!rawPrefix.isEmpty())
        {
            const qsizetype begin = offset;
            const int stringLine = line;
            const qsizetype delimiterBegin = offset + rawPrefix.size();
            const qsizetype delimiterEnd = source.indexOf(QLatin1Char('('), delimiterBegin);
            if (delimiterEnd < 0)
            {
                result.errors.append(QStringLiteral("line %1: malformed raw string").arg(stringLine));
                break;
            }
            const QString delimiter = source.sliced(delimiterBegin, delimiterEnd - delimiterBegin);
            const bool malformedDelimiter = delimiter.size() > 16 ||
                std::any_of(delimiter.cbegin(), delimiter.cend(), [](const QChar character) {
                    return character.isSpace() || character == QLatin1Char(')') || character == QLatin1Char('\\');
                });
            if (malformedDelimiter)
            {
                result.errors.append(QStringLiteral("line %1: malformed raw string delimiter").arg(stringLine));
                break;
            }
            const QString terminator = QStringLiteral(")") + delimiter + QLatin1Char('"');
            const qsizetype end = source.indexOf(terminator, delimiterEnd + 1);
            if (end < 0)
            {
                result.errors.append(QStringLiteral("line %1: unterminated raw string").arg(stringLine));
                break;
            }
            offset = end + terminator.size();
            const QString value = source.sliced(begin, offset - begin);
            line += value.count(QLatin1Char('\n'));
            appendToken(Token::Kind::String, begin, offset, stringLine);
            continue;
        }

        if (character == QLatin1Char('\'') || character == QLatin1Char('"') || character == QLatin1Char('`'))
        {
            const qsizetype begin = offset;
            const int stringLine = line;
            const QChar quote = character;
            ++offset;
            bool closed = false;
            while (offset < source.size())
            {
                if (source.at(offset) == QLatin1Char('\\'))
                {
                    offset += std::min<qsizetype>(2, source.size() - offset);
                    continue;
                }
                if (source.at(offset) == quote)
                {
                    ++offset;
                    closed = true;
                    break;
                }
                if (source.at(offset) == QLatin1Char('\n'))
                {
                    ++line;
                }
                ++offset;
            }
            if (!closed)
            {
                result.errors.append(QStringLiteral("line %1: unterminated string").arg(stringLine));
                break;
            }
            appendToken(Token::Kind::String, begin, offset, stringLine);
            continue;
        }

        if (isIdentifierStart(character))
        {
            const qsizetype begin = offset++;
            while (offset < source.size() && isIdentifierPart(source.at(offset)))
            {
                ++offset;
            }
            appendToken(Token::Kind::Identifier, begin, offset, line);
            continue;
        }

        if (character.isDigit())
        {
            const qsizetype begin = offset++;
            while (offset < source.size() && (source.at(offset).isLetterOrNumber() || source.at(offset) == QLatin1Char('.')))
            {
                ++offset;
            }
            appendToken(Token::Kind::Number, begin, offset, line);
            continue;
        }

        static const QStringList operators{
            QStringLiteral("==="), QStringLiteral("!=="), QStringLiteral("->"), QStringLiteral("=>"), QStringLiteral("::"),
            QStringLiteral("=="),  QStringLiteral("!="),  QStringLiteral("<="), QStringLiteral(">="), QStringLiteral("&&"),
            QStringLiteral("||"),  QStringLiteral("++"),  QStringLiteral("--"), QStringLiteral("+="), QStringLiteral("-="),
            QStringLiteral("*="),  QStringLiteral("/="),  QStringLiteral("?."),
        };
        bool operatorFound = false;
        for (const QString &candidate : operators)
        {
            if (sourceView.sliced(offset).startsWith(candidate))
            {
                appendToken(Token::Kind::Symbol, offset, offset + candidate.size(), line);
                offset += candidate.size();
                operatorFound = true;
                break;
            }
        }
        if (operatorFound)
        {
            continue;
        }

        appendToken(Token::Kind::Symbol, offset, offset + 1, line);
        ++offset;
    }

    return result;
}

inline QHash<int, int> delimiterPairs(const QVector<Token> &tokens, QStringList &errors)
{
    struct Opening
    {
        QString text;
        int index;
    };
    QVector<Opening> stack;
    QHash<int, int> pairs;
    const QHash<QString, QString> closing{
        {QStringLiteral("}"), QStringLiteral("{")}, {QStringLiteral("]"), QStringLiteral("[")}, {QStringLiteral(")"), QStringLiteral("(")}};

    for (int index = 0; index < tokens.size(); ++index)
    {
        const QString &text = tokens.at(index).text;
        if (text == QLatin1String("{") || text == QLatin1String("[") || text == QLatin1String("("))
        {
            stack.append({text, index});
            continue;
        }
        if (!closing.contains(text))
        {
            continue;
        }
        if (stack.isEmpty() || stack.constLast().text != closing.value(text))
        {
            errors.append(QStringLiteral("line %1: unmatched %2").arg(tokens.at(index).line).arg(text));
            continue;
        }
        const Opening opening = stack.takeLast();
        pairs.insert(opening.index, index);
        pairs.insert(index, opening.index);
    }

    for (const Opening &opening : stack)
    {
        errors.append(QStringLiteral("line %1: unclosed %2").arg(tokens.at(opening.index).line).arg(opening.text));
    }
    return pairs;
}

inline bool signalHandler(const QString &name)
{
    const QString leaf = name.section(QLatin1Char('.'), -1);
    return leaf.size() > 2 && leaf.startsWith(QLatin1String("on")) && leaf.at(2).isUpper();
}

inline bool lifecycleMember(const QString &name)
{
    static const QSet<QString> members{
        QStringLiteral("Component.onCompleted"),
        QStringLiteral("Component.onDestruction"),
    };
    return members.contains(name);
}

inline std::optional<QPair<QString, int>> directName(const QVector<Token> &tokens, const int begin, const int end)
{
    if (begin >= end || tokens.at(begin).kind != Token::Kind::Identifier)
    {
        return std::nullopt;
    }
    QString name = tokens.at(begin).text;
    int cursor = begin + 1;
    while (cursor + 1 < end && tokens.at(cursor).text == QLatin1String(".") && tokens.at(cursor + 1).kind == Token::Kind::Identifier)
    {
        name += QLatin1Char('.') + tokens.at(cursor + 1).text;
        cursor += 2;
    }
    return QPair{name, cursor};
}

inline std::optional<QPair<QString, int>> qualifiedNameBefore(const QVector<Token> &tokens, const int brace)
{
    if (brace == 0 || tokens.at(brace - 1).kind != Token::Kind::Identifier)
    {
        return std::nullopt;
    }
    QStringList parts{tokens.at(brace - 1).text};
    int start = brace - 1;
    while (start >= 2 && tokens.at(start - 1).text == QLatin1String(".") && tokens.at(start - 2).kind == Token::Kind::Identifier)
    {
        parts.prepend(tokens.at(start - 2).text);
        start -= 2;
    }
    static const QSet<QString> blocks{
        QStringLiteral("catch"),    QStringLiteral("do"), QStringLiteral("else"),   QStringLiteral("finally"), QStringLiteral("for"),
        QStringLiteral("function"), QStringLiteral("if"), QStringLiteral("switch"), QStringLiteral("try"),     QStringLiteral("while")};
    if (parts.constLast().isEmpty() || !parts.constLast().at(0).isUpper() || blocks.contains(parts.constLast()))
    {
        return std::nullopt;
    }
    return QPair{parts.join(QLatin1Char('.')), start};
}

inline bool interactiveType(const QString &qualifiedType)
{
    const QString leaf = qualifiedType.section(QLatin1Char('.'), -1);
    static const QSet<QString> exact{
        QStringLiteral("Action"),       QStringLiteral("Button"),         QStringLiteral("CheckBox"),         QStringLiteral("CheckDelegate"),
        QStringLiteral("ColorDialog"),
        QStringLiteral("ComboBox"),     QStringLiteral("ComboBoxButton"), QStringLiteral("ContextMenuLayer"), QStringLiteral("DragHandler"),
        QStringLiteral("HeaderSwitch"), QStringLiteral("ItemDelegate"),   QStringLiteral("Menu"),             QStringLiteral("MenuItem"),
        QStringLiteral("MouseArea"),
        QStringLiteral("ScrollArea"),   QStringLiteral("ScrollView"),     QStringLiteral("Slider"),           QStringLiteral("SpinBox"),
        QStringLiteral("Switch"),
        QStringLiteral("TabButton"),    QStringLiteral("TapHandler"),     QStringLiteral("TextField"),        QStringLiteral("Timer"),
        QStringLiteral("ToolButton"),   QStringLiteral("WheelHandler"),
    };
    static const QStringList suffixes{QStringLiteral("Button"),  QStringLiteral("CheckBox"), QStringLiteral("ComboBox"),
                                      QStringLiteral("Handler"), QStringLiteral("MenuItem"), QStringLiteral("Slider"),
                                      QStringLiteral("Switch"),  QStringLiteral("TextField")};
    if (exact.contains(leaf))
    {
        return true;
    }
    return std::any_of(suffixes.cbegin(), suffixes.cend(), [&](const QString &suffix) { return leaf.endsWith(suffix); });
}

struct Member
{
    QString name;
    int line{1};
};

struct ModelElement
{
    QString binding;
    QString key;
    int line{1};
};

struct QmlObject
{
    QString type;
    QString id;
    int typeStart{0};
    int brace{0};
    int end{0};
    int parent{-1};
    QString digest;
    QString anchor;
    QList<Member> members;
    QList<ModelElement> modelElements;
};

inline QString modelDiscriminator(const QVector<Token> &tokens, const int begin, const int end)
{
    static const QStringList preferred{QStringLiteral("value"), QStringLiteral("pluginId"), QStringLiteral("actionId"),
                                       QStringLiteral("name")};
    for (const QString &property : preferred)
    {
        for (int index = begin; index + 2 < end; ++index)
        {
            if (tokens.at(index).text != property || tokens.at(index + 1).text != QLatin1String(":"))
            {
                continue;
            }
            int valueEnd = index + 2;
            int depth = 0;
            while (valueEnd < end)
            {
                const QString &text = tokens.at(valueEnd).text;
                if (depth == 0 && (text == QLatin1String(",") || text == QLatin1String("}")))
                {
                    break;
                }
                if (text == QLatin1String("{") || text == QLatin1String("[") || text == QLatin1String("("))
                {
                    ++depth;
                }
                else if (text == QLatin1String("}") || text == QLatin1String("]") || text == QLatin1String(")"))
                {
                    --depth;
                }
                ++valueEnd;
            }
            return QStringLiteral("%1:%2").arg(property, normalizedTokens(tokens, index + 2, valueEnd));
        }
    }
    return QStringLiteral("sha256:%1").arg(tokenDigest(tokens, begin, end));
}

inline void addResolvable(ScanResult &result, const QString &selector, const int line)
{
    result.resolvableLines[selector].append(line);
}

inline void addCandidate(ScanResult &result, const QString &selector, const QString &category, const int line)
{
    addResolvable(result, selector, line);
    result.candidates.append({selector, category, line});
}

inline void finishCandidates(ScanResult &result)
{
    QHash<QString, QList<int>> candidateLines;
    for (const SourceSite &candidate : result.candidates)
    {
        candidateLines[candidate.selector].append(candidate.line);
    }
    for (auto it = candidateLines.cbegin(); it != candidateLines.cend(); ++it)
    {
        if (it.value().size() > 1)
        {
            QStringList lines;
            for (const int line : it.value())
            {
                lines.append(QString::number(line));
            }
            result.errors.append(QStringLiteral("ambiguous candidate selector %1 at lines %2").arg(it.key(), lines.join(QLatin1Char(','))));
        }
    }
}

inline QString qmlAnchor(const QList<QmlObject> &objects, const int index)
{
    const QmlObject &object = objects.at(index);
    const QString local =
        object.id.isEmpty() ? QStringLiteral("type:%1#sha256:%2").arg(object.type, object.digest) : QStringLiteral("id:%1").arg(object.id);
    if (object.parent < 0)
    {
        return local;
    }
    if (!object.id.isEmpty())
    {
        // Named objects need only named ancestry. Skipping anonymous layout
        // wrappers prevents a visual parent edit from invalidating every id
        // beneath it while still distinguishing ids in nested components.
        int namedParent = object.parent;
        while (namedParent >= 0 && objects.at(namedParent).id.isEmpty())
        {
            namedParent = objects.at(namedParent).parent;
        }
        return namedParent < 0 ? local : qmlAnchor(objects, namedParent) + QLatin1Char('/') + local;
    }
    return qmlAnchor(objects, object.parent) + QLatin1Char('/') + local;
}

struct CppScope
{
    int begin{0};
    int end{0};
    QString identity;
};

inline QString enclosingScope(const QVector<CppScope> &scopes, const int tokenIndex)
{
    QString identity = QStringLiteral("<global>");
    int narrowest = std::numeric_limits<int>::max();
    for (const CppScope &scope : scopes)
    {
        if (scope.begin < tokenIndex && tokenIndex < scope.end && scope.end - scope.begin < narrowest)
        {
            identity = scope.identity;
            narrowest = scope.end - scope.begin;
        }
    }
    return identity;
}

inline QString functionIdentityBefore(const QVector<Token> &tokens, const QHash<int, int> &pairs, const int brace)
{
    int closeParen = brace - 1;
    while (closeParen >= 0 &&
           (tokens.at(closeParen).text == QLatin1String("const") || tokens.at(closeParen).text == QLatin1String("noexcept") ||
            tokens.at(closeParen).text == QLatin1String("override")))
    {
        --closeParen;
    }
    if (closeParen < 0 || tokens.at(closeParen).text != QLatin1String(")") || !pairs.contains(closeParen))
    {
        return {};
    }
    const int openParen = pairs.value(closeParen);
    if (openParen == 0 || tokens.at(openParen - 1).kind != Token::Kind::Identifier)
    {
        return {};
    }
    QString name = tokens.at(openParen - 1).text;
    static const QSet<QString> controlStatements{
        QStringLiteral("catch"), QStringLiteral("for"), QStringLiteral("if"), QStringLiteral("switch"), QStringLiteral("while")};
    if (controlStatements.contains(name))
    {
        return {};
    }
    int cursor = openParen - 2;
    while (cursor >= 1 && tokens.at(cursor).text == QLatin1String("::") && tokens.at(cursor - 1).kind == Token::Kind::Identifier)
    {
        name.prepend(tokens.at(cursor - 1).text + QStringLiteral("::"));
        cursor -= 2;
    }
    return QStringLiteral("function:%1|signature-sha256:%2").arg(name, tokenDigest(tokens, openParen, brace));
}

inline int lambdaHeaderBegin(const QVector<Token> &tokens, const QHash<int, int> &pairs, const int brace)
{
    // Lambda suffixes can contain parameters, specifiers, attributes, and a
    // trailing return. Walk back to the non-attribute capture instead of
    // assuming that its closing bracket immediately precedes the body.
    for (int cursor = brace - 1; cursor >= 0; --cursor)
    {
        const QString &text = tokens.at(cursor).text;
        if (text == QLatin1String(";") || text == QLatin1String("{") || text == QLatin1String("}"))
        {
            return -1;
        }
        if (text != QLatin1String("]") || !pairs.contains(cursor))
        {
            continue;
        }
        const int open = pairs.value(cursor);
        const bool isAttribute = (open > 0 && tokens.at(open - 1).text == QLatin1String("[")) ||
            (open + 1 < tokens.size() && tokens.at(open + 1).text == QLatin1String("["));
        if (isAttribute)
        {
            cursor = open;
            continue;
        }
        const int after = cursor + 1;
        if (after == brace || tokens.at(after).text == QLatin1String("(") || tokens.at(after).text == QLatin1String("mutable") ||
            tokens.at(after).text == QLatin1String("constexpr") || tokens.at(after).text == QLatin1String("consteval") ||
            tokens.at(after).text == QLatin1String("noexcept") || tokens.at(after).text == QLatin1String("[") ||
            tokens.at(after).text == QLatin1String("->"))
        {
            return open;
        }
        cursor = open;
    }
    return -1;
}

inline std::optional<QPair<QString, int>> qualifiedTypeAt(const QVector<Token> &tokens, const int begin, const int end)
{
    if (begin >= end)
    {
        return std::nullopt;
    }
    const bool globallyQualified = tokens.at(begin).text == QLatin1String("::");
    const int firstIdentifier = begin + (globallyQualified ? 1 : 0);
    if (firstIdentifier >= end || tokens.at(firstIdentifier).kind != Token::Kind::Identifier)
    {
        return std::nullopt;
    }
    QStringList parts{tokens.at(firstIdentifier).text};
    int cursor = firstIdentifier + 1;
    while (cursor + 1 < end && tokens.at(cursor).text == QLatin1String("::") &&
           tokens.at(cursor + 1).kind == Token::Kind::Identifier)
    {
        parts.append(tokens.at(cursor + 1).text);
        cursor += 2;
    }
    return QPair{(globallyQualified ? QStringLiteral("::") : QString()) + parts.join(QStringLiteral("::")), cursor};
}

inline QHash<QString, QString> constructedTypeAliases(const QVector<Token> &tokens)
{
    static const QSet<QString> constructedTypes{QStringLiteral("QAction"), QStringLiteral("QWidgetAction"), QStringLiteral("QMenu")};
    QHash<QString, QString> aliases;
    for (int index = 0; index < tokens.size(); ++index)
    {
        if (tokens.at(index).text == QLatin1String("using") && index + 3 < tokens.size() &&
            tokens.at(index + 1).kind == Token::Kind::Identifier && tokens.at(index + 2).text == QLatin1String("="))
        {
            const auto type = qualifiedTypeAt(tokens, index + 3, tokens.size());
            if (type && constructedTypes.contains(type->first.section(QLatin1Char(':'), -1)))
            {
                aliases.insert(tokens.at(index + 1).text, type->first.section(QLatin1Char(':'), -1));
            }
        }
        if (tokens.at(index).text != QLatin1String("typedef"))
        {
            continue;
        }
        int end = index + 1;
        while (end < tokens.size() && tokens.at(end).text != QLatin1String(";"))
        {
            ++end;
        }
        const auto type = qualifiedTypeAt(tokens, index + 1, end);
        if (!type || !constructedTypes.contains(type->first.section(QLatin1Char(':'), -1)) || end - 1 < type->second)
        {
            continue;
        }
        const QString alias = tokens.at(end - 1).text;
        if (tokens.at(end - 1).kind == Token::Kind::Identifier)
        {
            aliases.insert(alias, type->first.section(QLatin1Char(':'), -1));
        }
    }
    return aliases;
}

inline QPair<int, int> statementBounds(const QVector<Token> &tokens, const int index)
{
    int begin = index;
    while (begin > 0 && tokens.at(begin - 1).text != QLatin1String(";") && tokens.at(begin - 1).text != QLatin1String("{") &&
           tokens.at(begin - 1).text != QLatin1String("}"))
    {
        --begin;
    }
    int end = index;
    while (end < tokens.size() && tokens.at(end).text != QLatin1String(";"))
    {
        ++end;
    }
    if (end < tokens.size())
    {
        ++end;
    }
    return {begin, end};
}

inline int enclosingQmlObject(const QList<QmlObject> &objects, const int tokenIndex)
{
    int enclosing = -1;
    int narrowest = std::numeric_limits<int>::max();
    for (int index = 0; index < objects.size(); ++index)
    {
        const QmlObject &object = objects.at(index);
        if (object.brace < tokenIndex && tokenIndex < object.end && object.end - object.brace < narrowest)
        {
            enclosing = index;
            narrowest = object.end - object.brace;
        }
    }
    return enclosing;
}

inline QString qmlScopeIdentity(const QVector<Token> &tokens, const QHash<int, int> &pairs, const QmlObject &object,
                                const int tokenIndex)
{
    QStringList ancestry{QStringLiteral("object")};
    for (int brace = object.brace + 1; brace < tokenIndex; ++brace)
    {
        if (tokens.at(brace).text != QLatin1String("{") || !pairs.contains(brace) || pairs.value(brace) <= tokenIndex)
        {
            continue;
        }
        QString candidate;
        if (brace >= 2 && tokens.at(brace - 1).text == QLatin1String(":") && signalHandler(tokens.at(brace - 2).text))
        {
            candidate = QStringLiteral("handler:%1").arg(tokens.at(brace - 2).text);
        }
        else if (brace > 0 && tokens.at(brace - 1).text == QLatin1String(")") && pairs.contains(brace - 1))
        {
            const int openParen = pairs.value(brace - 1);
            if (openParen >= 2 && tokens.at(openParen - 2).text == QLatin1String("function") &&
                tokens.at(openParen - 1).kind == Token::Kind::Identifier)
            {
                candidate = QStringLiteral("function:%1").arg(tokens.at(openParen - 1).text);
            }
            else if (openParen > 0)
            {
                static const QSet<QString> controlBlocks{
                    QStringLiteral("for"), QStringLiteral("if"), QStringLiteral("switch"), QStringLiteral("while")};
                const QString control = tokens.at(openParen - 1).text;
                if (controlBlocks.contains(control))
                {
                    candidate = QStringLiteral("block:%1#sha256:%2").arg(control, tokenDigest(tokens, openParen - 1, brace));
                }
            }
        }
        else if (brace > 0 && tokens.at(brace - 1).text == QLatin1String("else"))
        {
            candidate = QStringLiteral("block:else#sha256:%1").arg(tokenDigest(tokens, brace - 1, brace));
        }
        if (!candidate.isEmpty())
        {
            ancestry.append(candidate);
        }
    }
    return ancestry.join(QLatin1Char('/'));
}

struct QmlSemanticCall
{
    int operationIndex{0};
    int openParen{0};
    int closeParen{0};
    int statementBegin{0};
    int statementEnd{0};
    QString operation;
};

inline bool qmlActionCreationOperation(const QString &operation)
{
    static const QSet<QString> operations{QStringLiteral("newMenuItem"), QStringLiteral("createMenuItem"),
                                          QStringLiteral("createNewItem"), QStringLiteral("newSeparator")};
    return operations.contains(operation);
}

inline QList<QmlSemanticCall> semanticQmlCalls(const QVector<Token> &tokens, const QHash<int, int> &pairs)
{
    // Only operations that construct, connect, or insert dynamic menu actions
    // are candidates. General JavaScript calls stay outside the universe, so
    // ordinary model and rendering helpers cannot inflate settings coverage.
    QList<QmlSemanticCall> calls;
    for (int index = 0; index + 1 < tokens.size(); ++index)
    {
        if (tokens.at(index).kind != Token::Kind::Identifier || tokens.at(index + 1).text != QLatin1String("(") ||
            !pairs.contains(index + 1))
        {
            continue;
        }
        if (index > 0 && tokens.at(index - 1).text == QLatin1String("function"))
        {
            continue;
        }
        QString operation;
        if (qmlActionCreationOperation(tokens.at(index).text) || tokens.at(index).text == QLatin1String("addMenuItem"))
        {
            operation = tokens.at(index).text;
        }
        else if (tokens.at(index).text == QLatin1String("connect") && index >= 2 &&
                 tokens.at(index - 1).text == QLatin1String(".") && tokens.at(index - 2).text == QLatin1String("clicked"))
        {
            operation = QStringLiteral("clicked.connect");
        }
        else if (tokens.at(index).text == QLatin1String("createQmlObject") && index >= 2 &&
                 tokens.at(index - 1).text == QLatin1String(".") && tokens.at(index - 2).text == QLatin1String("Qt"))
        {
            const int closeParen = pairs.value(index + 1);
            bool createsMenuItem = false;
            for (int argument = index + 2; argument < closeParen; ++argument)
            {
                createsMenuItem = createsMenuItem ||
                    (tokens.at(argument).kind == Token::Kind::String && tokens.at(argument).text.contains(QLatin1String("MenuItem")));
            }
            if (createsMenuItem)
            {
                operation = QStringLiteral("Qt.createQmlObject<MenuItem>");
            }
        }
        if (operation.isEmpty())
        {
            continue;
        }

        const auto [statementBegin, ignoredEnd] = statementBounds(tokens, index);
        const int closeParen = pairs.value(index + 1);
        int statementEnd = closeParen + 1;
        if (statementEnd < tokens.size() && tokens.at(statementEnd).text == QLatin1String(";"))
        {
            ++statementEnd;
        }
        calls.append({index, index + 1, closeParen, statementBegin, statementEnd, operation});
    }
    return calls;
}

inline int qmlCallExpressionBegin(const QVector<Token> &tokens, const int operationIndex)
{
    int begin = operationIndex;
    while (begin >= 2 && tokens.at(begin - 1).text == QLatin1String(".") &&
           tokens.at(begin - 2).kind == Token::Kind::Identifier)
    {
        begin -= 2;
    }
    return begin;
}

inline QPair<int, int> qmlActionBundle(const QVector<Token> &tokens, const QList<QmlSemanticCall> &calls,
                                      const QStringList &scopes, const int callIndex)
{
    const QmlSemanticCall &call = calls.at(callIndex);
    int begin = call.statementBegin;
    int end = call.statementEnd;

    for (int parentIndex = 0; parentIndex < calls.size(); ++parentIndex)
    {
        const QmlSemanticCall &possibleParent = calls.at(parentIndex);
        if (possibleParent.operation == QLatin1String("addMenuItem") && possibleParent.openParen < call.operationIndex &&
            call.operationIndex < possibleParent.closeParen && scopes.at(parentIndex) == scopes.at(callIndex))
        {
            return {possibleParent.statementBegin, possibleParent.statementEnd};
        }
    }
    if (call.operation == QLatin1String("newSeparator"))
    {
        return {qmlCallExpressionBegin(tokens, call.operationIndex), end};
    }
    const bool isCreation = qmlActionCreationOperation(call.operation);
    for (int previous = callIndex - 1; !isCreation && previous >= 0; --previous)
    {
        if (scopes.at(previous) != scopes.at(callIndex))
        {
            break;
        }
        if (calls.at(previous).operation == QLatin1String("addMenuItem"))
        {
            break;
        }
        if (qmlActionCreationOperation(calls.at(previous).operation))
        {
            begin = calls.at(previous).statementBegin;
            break;
        }
    }
    for (int next = callIndex; next < calls.size(); ++next)
    {
        if (scopes.at(next) != scopes.at(callIndex))
        {
            break;
        }
        if (calls.at(next).operation == QLatin1String("addMenuItem"))
        {
            end = calls.at(next).statementEnd;
            break;
        }
        if (next > callIndex && qmlActionCreationOperation(calls.at(next).operation))
        {
            break;
        }
        if (calls.at(next).operation == QLatin1String("clicked.connect"))
        {
            end = calls.at(next).statementEnd;
        }
    }
    return {begin, end};
}

inline QString qmlActionRightBoundaryDigest(const QVector<Token> &tokens, const QList<QmlSemanticCall> &calls,
                                            const QStringList &scopes, const int callIndex)
{
    // Repeated separator statements can be identical inside one function. The
    // following creation bundle disambiguates them without absorbing the
    // preceding action or introducing a line/occurrence index.
    const QString scope = scopes.at(callIndex);
    for (int next = callIndex + 1; next < calls.size(); ++next)
    {
        if (scopes.at(next) != scope && !scopes.at(next).startsWith(scope + QLatin1Char('/')))
        {
            break;
        }
        if (qmlActionCreationOperation(calls.at(next).operation))
        {
            const auto [ignoredBegin, bundleEnd] = qmlActionBundle(tokens, calls, scopes, next);
            return tokenDigest(tokens, qmlCallExpressionBegin(tokens, calls.at(next).operationIndex), bundleEnd);
        }
    }
    return sha256(QByteArrayLiteral("<scope-end>"));
}

} // namespace Detail

inline ScanResult scanQml(const QString &source)
{
    using namespace Detail;
    const LexResult lexical = lex(source);
    ScanResult result;
    result.errors = lexical.errors;
    for (const SourceSite &comment : lexical.comments)
    {
        addResolvable(result, comment.selector, comment.line);
    }
    addResolvable(result, QStringLiteral("file"), 1);
    if (!result.errors.isEmpty())
    {
        return result;
    }

    QStringList delimiterErrors;
    const QHash<int, int> pairs = delimiterPairs(lexical.tokens, delimiterErrors);
    result.errors.append(delimiterErrors);
    if (!result.errors.isEmpty())
    {
        return result;
    }

    QList<QmlObject> objects;
    for (int brace = 0; brace < lexical.tokens.size(); ++brace)
    {
        if (lexical.tokens.at(brace).text != QLatin1String("{") || !pairs.contains(brace))
        {
            continue;
        }
        const auto qualified = qualifiedNameBefore(lexical.tokens, brace);
        if (!qualified)
        {
            continue;
        }
        QmlObject object;
        object.type = qualified->first;
        object.typeStart = qualified->second;
        object.brace = brace;
        object.end = pairs.value(brace);
        object.digest = tokenDigest(lexical.tokens, object.typeStart, object.end + 1);

        int cursor = brace + 1;
        while (cursor < object.end)
        {
            const QString &text = lexical.tokens.at(cursor).text;
            if (text == QLatin1String("{") || text == QLatin1String("[") || text == QLatin1String("("))
            {
                cursor = pairs.value(cursor, cursor) + 1;
                continue;
            }
            if (cursor > brace + 1 && lexical.tokens.at(cursor - 1).text == QLatin1String("."))
            {
                ++cursor;
                continue;
            }
            if (text == QLatin1String("function"))
            {
                const auto name = directName(lexical.tokens, cursor + 1, object.end);
                if (name && signalHandler(name->first))
                {
                    object.members.append({name->first, lexical.tokens.at(cursor).line});
                }
                ++cursor;
                continue;
            }
            const auto name = directName(lexical.tokens, cursor, object.end);
            if (!name || name->second >= object.end || lexical.tokens.at(name->second).text != QLatin1String(":"))
            {
                ++cursor;
                continue;
            }
            const int value = name->second + 1;
            if (name->first == QLatin1String("id") && value < object.end)
            {
                object.id = lexical.tokens.at(value).text;
            }
            if (signalHandler(name->first) || name->first == QLatin1String("acceptedButtons"))
            {
                object.members.append({name->first, lexical.tokens.at(cursor).line});
            }
            if ((name->first == QLatin1String("model") || name->first == QLatin1String("modes")) && value < object.end &&
                lexical.tokens.at(value).text == QLatin1String("[") && pairs.contains(value))
            {
                const int arrayEnd = pairs.value(value);
                int elementBegin = value + 1;
                int depth = 0;
                for (int elementCursor = value + 1; elementCursor <= arrayEnd; ++elementCursor)
                {
                    const QString elementText = lexical.tokens.at(elementCursor).text;
                    if (elementText == QLatin1String("{") || elementText == QLatin1String("[") || elementText == QLatin1String("("))
                    {
                        ++depth;
                    }
                    else if (elementText == QLatin1String("}") || elementText == QLatin1String("]") || elementText == QLatin1String(")"))
                    {
                        --depth;
                    }
                    if ((elementText == QLatin1String(",") && depth == 0) || elementCursor == arrayEnd)
                    {
                        if (elementBegin < elementCursor)
                        {
                            object.modelElements.append({name->first, modelDiscriminator(lexical.tokens, elementBegin, elementCursor),
                                                         lexical.tokens.at(elementBegin).line});
                        }
                        elementBegin = elementCursor + 1;
                    }
                }
            }
            ++cursor;
        }
        objects.append(object);
    }

    for (int index = 0; index < objects.size(); ++index)
    {
        int nearest = -1;
        int nearestSpan = std::numeric_limits<int>::max();
        for (int candidate = 0; candidate < objects.size(); ++candidate)
        {
            if (candidate == index || objects.at(candidate).brace >= objects.at(index).brace ||
                objects.at(candidate).end <= objects.at(index).end)
            {
                continue;
            }
            const int span = objects.at(candidate).end - objects.at(candidate).brace;
            if (span < nearestSpan)
            {
                nearest = candidate;
                nearestSpan = span;
            }
        }
        objects[index].parent = nearest;
    }
    for (int index = 0; index < objects.size(); ++index)
    {
        objects[index].anchor = qmlAnchor(objects, index);
    }

    for (const QmlObject &object : std::as_const(objects))
    {
        const bool objectIsInteractive = interactiveType(object.type);
        const QString declaration = QStringLiteral("qml-object|%1").arg(object.anchor);
        if (objectIsInteractive)
        {
            addCandidate(result, declaration, QStringLiteral("qml-object"), lexical.tokens.at(object.typeStart).line);
        }
        else
        {
            addResolvable(result, declaration, lexical.tokens.at(object.typeStart).line);
        }
        const QString leaf = object.type.section(QLatin1Char('.'), -1);
        for (const Member &member : object.members)
        {
            const QString selector = QStringLiteral("qml-member|%1|name:%2").arg(object.anchor, member.name);
            const bool attached = member.name.startsWith(QLatin1String("Keys.")) || member.name.startsWith(QLatin1String("Accessible."));
            if (objectIsInteractive || leaf == QLatin1String("Connections") || attached || lifecycleMember(member.name))
            {
                addCandidate(result, selector, QStringLiteral("qml-member"), member.line);
            }
            else
            {
                addResolvable(result, selector, member.line);
            }
        }
        if (objectIsInteractive)
        {
            for (const ModelElement &element : object.modelElements)
            {
                const QString selector = QStringLiteral("qml-model|%1|binding:%2|key:%3").arg(object.anchor, element.binding, element.key);
                addCandidate(result, selector, QStringLiteral("qml-model"), element.line);
            }
        }
    }

    // Dynamic ListModel producers are resolvable representatives, not
    // automatic candidates. The checked ledger must name them explicitly.
    for (int index = 0; index + 3 < lexical.tokens.size(); ++index)
    {
        if ((lexical.tokens.at(index).text != QLatin1String("var") && lexical.tokens.at(index).text != QLatin1String("let") &&
             lexical.tokens.at(index).text != QLatin1String("const")) ||
            lexical.tokens.at(index + 1).kind != Token::Kind::Identifier || lexical.tokens.at(index + 2).text != QLatin1String("=") ||
            lexical.tokens.at(index + 3).text != QLatin1String("{") || !pairs.contains(index + 3))
        {
            continue;
        }
        const int end = pairs.value(index + 3);
        const QString selector = QStringLiteral("qml-js-object|name:%1|sha256:%2")
                                     .arg(lexical.tokens.at(index + 1).text, tokenDigest(lexical.tokens, index + 3, end + 1));
        addResolvable(result, selector, lexical.tokens.at(index).line);
    }

    const QList<QmlSemanticCall> dynamicCalls = semanticQmlCalls(lexical.tokens, pairs);
    QStringList dynamicScopes;
    dynamicScopes.reserve(dynamicCalls.size());
    for (const QmlSemanticCall &call : dynamicCalls)
    {
        const int objectIndex = enclosingQmlObject(objects, call.operationIndex);
        dynamicScopes.append(objectIndex < 0
                                 ? QStringLiteral("<missing>")
                                 : QStringLiteral("%1|%2").arg(objects.at(objectIndex).anchor,
                                                                qmlScopeIdentity(lexical.tokens, pairs, objects.at(objectIndex),
                                                                                 call.operationIndex)));
    }
    for (int index = 0; index < dynamicCalls.size(); ++index)
    {
        const QmlSemanticCall &call = dynamicCalls.at(index);
        const int objectIndex = enclosingQmlObject(objects, call.operationIndex);
        if (objectIndex < 0)
        {
            result.errors.append(QStringLiteral("line %1: dynamic action call has no enclosing QML object")
                                     .arg(lexical.tokens.at(call.operationIndex).line));
            continue;
        }
        const QmlObject &object = objects.at(objectIndex);
        const auto [bundleBegin, bundleEnd] = qmlActionBundle(lexical.tokens, dynamicCalls, dynamicScopes, index);
        QString selector = QStringLiteral("qml-call|%1|scope:%2|operation:%3")
                               .arg(object.anchor,
                                    qmlScopeIdentity(lexical.tokens, pairs, object, call.operationIndex), call.operation);
        if (call.operation == QLatin1String("newSeparator"))
        {
            selector += QStringLiteral("|right-boundary-sha256:%1")
                            .arg(qmlActionRightBoundaryDigest(lexical.tokens, dynamicCalls, dynamicScopes, index));
        }
        selector += QStringLiteral("|bundle-sha256:%1").arg(tokenDigest(lexical.tokens, bundleBegin, bundleEnd));
        addCandidate(result, selector, QStringLiteral("qml-call"), lexical.tokens.at(call.operationIndex).line);
    }

    finishCandidates(result);
    return result;
}

inline ScanResult scanCpp(const QString &source)
{
    using namespace Detail;
    const LexResult lexical = lex(source);
    ScanResult result;
    result.errors = lexical.errors;
    for (const SourceSite &comment : lexical.comments)
    {
        addResolvable(result, comment.selector, comment.line);
    }
    addResolvable(result, QStringLiteral("file"), 1);
    if (!result.errors.isEmpty())
    {
        return result;
    }

    QStringList delimiterErrors;
    const QHash<int, int> pairs = delimiterPairs(lexical.tokens, delimiterErrors);
    result.errors.append(delimiterErrors);
    if (!result.errors.isEmpty())
    {
        return result;
    }

    QVector<CppScope> scopes;
    for (int index = 0; index < lexical.tokens.size(); ++index)
    {
        if (lexical.tokens.at(index).text != QLatin1String("{") || !pairs.contains(index))
        {
            continue;
        }
        const QString identity = functionIdentityBefore(lexical.tokens, pairs, index);
        if (!identity.isEmpty())
        {
            scopes.append({index, pairs.value(index), identity});
        }
    }
    for (int index = 0; index < lexical.tokens.size(); ++index)
    {
        if (lexical.tokens.at(index).text != QLatin1String("{") || !pairs.contains(index))
        {
            continue;
        }
        const int headerBegin = lambdaHeaderBegin(lexical.tokens, pairs, index);
        if (headerBegin >= 0)
        {
            const QString parent = enclosingScope(scopes, index);
            scopes.append({index, pairs.value(index),
                           QStringLiteral("%1/lambda-sha256:%2").arg(parent, tokenDigest(lexical.tokens, headerBegin, index))});
        }
    }

    QSet<QString> statementSelectors;
    for (int index = 0; index < lexical.tokens.size(); ++index)
    {
        if (lexical.tokens.at(index).text != QLatin1String(";"))
        {
            continue;
        }
        const auto [begin, end] = statementBounds(lexical.tokens, index);
        if (begin >= end)
        {
            continue;
        }
        const QString scope = enclosingScope(scopes, index);
        const QString selector =
            QStringLiteral("cpp-statement|scope:%1|sha256:%2").arg(scope, tokenDigest(lexical.tokens, begin, end));
        if (!statementSelectors.contains(selector))
        {
            addResolvable(result, selector, lexical.tokens.at(begin).line);
            statementSelectors.insert(selector);
        }
        else
        {
            addResolvable(result, selector, lexical.tokens.at(begin).line);
        }
    }

    static const QSet<QString> constructedTypes{QStringLiteral("QAction"), QStringLiteral("QWidgetAction"), QStringLiteral("QMenu")};
    static const QSet<QString> calls{QStringLiteral("addAction"), QStringLiteral("addSeparator"), QStringLiteral("menuAction")};
    const QHash<QString, QString> aliases = constructedTypeAliases(lexical.tokens);
    const auto canonicalType = [&](const QString &spelling) {
        const QString leaf = spelling.section(QLatin1Char(':'), -1);
        return aliases.value(leaf, leaf);
    };
    for (int index = 0; index < lexical.tokens.size(); ++index)
    {
        const QString scope = enclosingScope(scopes, index);
        if (lexical.tokens.at(index).text == QLatin1String("new") && index + 1 < lexical.tokens.size())
        {
            const auto type = qualifiedTypeAt(lexical.tokens, index + 1, lexical.tokens.size());
            if (!type || !constructedTypes.contains(canonicalType(type->first)))
            {
                continue;
            }
            const auto [begin, end] = statementBounds(lexical.tokens, index);
            int equal = index - 1;
            while (equal >= begin && lexical.tokens.at(equal).text != QLatin1String("="))
            {
                --equal;
            }
            const QString binding = equal >= begin ? normalizedTokens(lexical.tokens, begin, equal) : QStringLiteral("<temporary>");
            const QString selector =
                QStringLiteral("cpp-site|scope:%1|operation:construct|type:%2|spelling:%3|"
                               "binding:%4|sha256:%5")
                    .arg(scope, canonicalType(type->first), type->first, binding, tokenDigest(lexical.tokens, begin, end));
            addCandidate(result, selector, QStringLiteral("cpp-construction"), lexical.tokens.at(index).line);
            continue;
        }

        const auto [statementBegin, statementEnd] = statementBounds(lexical.tokens, index);
        const auto initializedType = qualifiedTypeAt(lexical.tokens, index, statementEnd);
        const bool followsAssignment = index > statementBegin && lexical.tokens.at(index - 1).text == QLatin1String("=");
        const bool inferredBinding = std::any_of(lexical.tokens.cbegin() + statementBegin, lexical.tokens.cbegin() + index,
                                                 [](const Token &token) { return token.text == QLatin1String("auto"); });
        // The declaration branch below owns typed copy initialization. This
        // branch discovers the constructed RHS when auto hides its type.
        if (scope != QLatin1String("<global>") && followsAssignment && inferredBinding && initializedType &&
            constructedTypes.contains(canonicalType(initializedType->first)) && initializedType->second < statementEnd &&
            (lexical.tokens.at(initializedType->second).text == QLatin1String("(") ||
             lexical.tokens.at(initializedType->second).text == QLatin1String("{")))
        {
            const QString binding = normalizedTokens(lexical.tokens, statementBegin, index - 1);
            const QString selector =
                QStringLiteral("cpp-site|scope:%1|operation:construct|type:%2|spelling:%3|binding:%4|sha256:%5")
                    .arg(scope, canonicalType(initializedType->first), initializedType->first, binding,
                         tokenDigest(lexical.tokens, statementBegin, statementEnd));
            addCandidate(result, selector, QStringLiteral("cpp-construction"), lexical.tokens.at(index).line);
            index = initializedType->second - 1;
            continue;
        }
        bool typeCanStartHere = index == statementBegin;
        for (int qualifier = statementBegin; !typeCanStartHere && qualifier < index; ++qualifier)
        {
            static const QSet<QString> qualifiers{QStringLiteral("const"), QStringLiteral("static"), QStringLiteral("thread_local")};
            if (!qualifiers.contains(lexical.tokens.at(qualifier).text))
            {
                break;
            }
            typeCanStartHere = qualifier + 1 == index;
        }
        const auto stackType = typeCanStartHere ? qualifiedTypeAt(lexical.tokens, index, statementEnd) : std::nullopt;
        if (scope != QLatin1String("<global>") && stackType && constructedTypes.contains(canonicalType(stackType->first)) &&
            stackType->second + 1 < statementEnd && lexical.tokens.at(stackType->second).kind == Token::Kind::Identifier &&
            lexical.tokens.at(stackType->second + 1).text != QLatin1String("*") &&
            (lexical.tokens.at(stackType->second + 1).text == QLatin1String("(") ||
             lexical.tokens.at(stackType->second + 1).text == QLatin1String("{") ||
             lexical.tokens.at(stackType->second + 1).text == QLatin1String("=") ||
             lexical.tokens.at(stackType->second + 1).text == QLatin1String(";")))
        {
            const QString binding = lexical.tokens.at(stackType->second).text;
            const QString selector =
                QStringLiteral("cpp-site|scope:%1|operation:construct|type:%2|spelling:%3|binding:%4|sha256:%5")
                    .arg(scope, canonicalType(stackType->first), stackType->first, binding,
                         tokenDigest(lexical.tokens, statementBegin, statementEnd));
            addCandidate(result, selector, QStringLiteral("cpp-construction"), lexical.tokens.at(index).line);
            index = stackType->second;
            continue;
        }
        if (!calls.contains(lexical.tokens.at(index).text) || index + 1 >= lexical.tokens.size() ||
            lexical.tokens.at(index + 1).text != QLatin1String("("))
        {
            continue;
        }
        const auto [begin, end] = statementBounds(lexical.tokens, index);
        QString receiver = QStringLiteral("<implicit>");
        if (index >= 2 &&
            (lexical.tokens.at(index - 1).text == QLatin1String(".") || lexical.tokens.at(index - 1).text == QLatin1String("->")))
        {
            int receiverBegin = index - 2;
            while (receiverBegin > begin && lexical.tokens.at(receiverBegin - 1).text != QLatin1String(";") &&
                   lexical.tokens.at(receiverBegin - 1).text != QLatin1String("{") &&
                   lexical.tokens.at(receiverBegin - 1).text != QLatin1String("}"))
            {
                --receiverBegin;
            }
            receiver = normalizedTokens(lexical.tokens, receiverBegin, index - 1);
        }
        const QString selector = QStringLiteral("cpp-site|scope:%1|operation:%2|receiver:%3|sha256:%4")
                                     .arg(scope, lexical.tokens.at(index).text, receiver, tokenDigest(lexical.tokens, begin, end));
        addCandidate(result, selector, QStringLiteral("cpp-call"), lexical.tokens.at(index).line);
    }

    finishCandidates(result);
    return result;
}

} // namespace Latte::SettingsSource

#endif
