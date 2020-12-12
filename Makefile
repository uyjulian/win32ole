
SOURCES += CArchive.cpp IDispatchWrapper.cpp main.cpp init_guid.c

LDLIBS += -lodbc32 -lodbccp32 -lgdi32 -lcomctl32 -lcomdlg32 -lole32 -loleaut32 -luuid

PROJECT_BASENAME = win32ole

RC_LEGALCOPYRIGHT ?= Copyright (C) 2010-2016 Go Watanabe; Copyright (C) 2008-2015 miahmie; Copyright (C) 2013 kiyobee; Copyright (C) 2019-2020 Julian Uy; See details of license at license.txt, or the source code location.

include external/ncbind/Rules.lib.make
