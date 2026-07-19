# Attribution roster for packaging

This file consolidates the unique `SPDX-FileCopyrightText` lines found in
the project source headers. Packagers for each format should translate this
roster into the format-specific copyright declaration:

- Debian: `debian/copyright` in DEP-5 format.
- RPM: `%license` plus `%files` attribution, or a bundled attribution file.
- Arch: PKGBUILD `license=()` and optional attribution comment.
- Gentoo: `LICENSE` and `METADATA.xml` `<upstream>` / `<use>` blocks.
- Void: template `license=` and `changelog=`.

Per-file licenses vary; the dominant identifiers are:

- `GPL-2.0-or-later` (most C++ sources and QML)
- `LGPL-2.0-or-later` (some utilities)
- `LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL`
  (tasktools derived from KDE Plasma taskmanager)

Each recipe must inspect the actual source headers under its installed
files and record the correct license for each copyright holder.

## Copyright holders

```
SPDX-FileCopyrightText: 2011 Aaron Seigo <aseigo@kde.org>
SPDX-FileCopyrightText: 2011 Daker Fernandes Pinheiro <dakerfp@gmail.com>
SPDX-FileCopyrightText: 2011 Marco Martin <mart@kde.org>
SPDX-FileCopyrightText: 2012-2016 Eike Hein <hein@kde.org>
SPDX-FileCopyrightText: 2012 Marco Martin <mart@kde.org>
SPDX-FileCopyrightText: 2013-2016 Eike Hein <hein@kde.org>
SPDX-FileCopyrightText: 2013 Marco Martin <mart@kde.org>
SPDX-FileCopyrightText: 2013 Sebastian Kügler <sebas@kde.org>
SPDX-FileCopyrightText: 2014 David Edmundson <davidedmudnson@kde.org>
SPDX-FileCopyrightText: 2014 Marco Martin <mart@kde.org>
SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2015 Kai Uwe Broulik <kde@privat.broulik.de>
SPDX-FileCopyrightText: 2016-2018 Michail Vourlakos <mvourlakos@gmail.com>
SPDX-FileCopyrightText: 2016, 2019 Kai Uwe Broulik <kde@privat.broulik.de>
SPDX-FileCopyrightText: 2016-2019 Michail Vourlakos <mvourlakos@gmail.com>
SPDX-FileCopyrightText: 2016 Eike Hein <hein.org>
SPDX-FileCopyrightText: 2016 Kai Uwe Broulik <kde@privat.broulik.de>
SPDX-FileCopyrightText: 2016 Marco Martin <mart@kde.org>
SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>
SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmaibox.org>
SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
SPDX-FileCopyrightText: 2017-2018 Michail Vourlakos <mvourlakos@gmail.com>
SPDX-FileCopyrightText: 2017 Kai Uwe Broulik <kde@privat.broulik.de>
SPDX-FileCopyrightText: 2017 Michail Vourlakos <mvourlakos@gmail.com>
SPDX-FileCopyrightText: 2017 Roman Gilg <subdiff@gmail.com>
SPDX-FileCopyrightText: 2017 Smith AR <audoban@openmailbox.org>
SPDX-FileCopyrightText: 2018 Marco Martin <mart@kde.org>
SPDX-FileCopyrightText: 2018 Michail Vourlakos <mvourlakos@gmail.com>
SPDX-FileCopyrightText: 2019-2020 Michail Vourlakos <mvourlakos@gmail.com>
SPDX-FileCopyrightText: 2019 Michail Vourlakos <mvourlakos@gmail.com>
SPDX-FileCopyrightText: 2020 Aleix Pol Gonzalez <aleixpol@kde.org>
SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
SPDX-FileCopyrightText: 2021 Aleix Pol Gonzalez <aleixpol@kde.org>
SPDX-FileCopyrightText: 2021 Michail Vourlakos <mvourlakos@gmail.com>
SPDX-FileCopyrightText: 2022 Luca Carlon<carlon.luca@gmail.com>
SPDX-FileCopyrightText: 2022 Michail Vourlakos <mvourlakos@gmail.com>
SPDX-FileCopyrightText: 2023 David Edmundson <davidedmundson@kde.org>
SPDX-FileCopyrightText: 2026 Bree Spektor
SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com>
SPDX-FileCopyrightText: 2026 Latte contributors
SPDX-FileCopyrightText: 2026 Latte Dock contributors
```

Additional translator and i18n credits are present in PO file headers under
`po/` and should be preserved by any packaging format that ships them.
