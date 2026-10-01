/*
 *    Copyright 2026 Michael Behrens
 *
 *    This file is part of OpenOrienteering.
 *
 *    OpenOrienteering is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    OpenOrienteering is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with OpenOrienteering.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "cli_t.h"

#include <cstring>
#include <string>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "cli.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_printer.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "global.h"
#include "test_config.h"

using namespace OpenOrienteering;


namespace {

/**
 * Builds a mutable argv array from string literals and calls execCli.
 * 
 * The caller must ensure the arguments remain valid for the duration of
 * the call (they do when using string literals).
 */
int execCliFrom(std::initializer_list<const char*> args)
{
	std::vector<char*> argv;
	argv.reserve(args.size());
	for (auto a : args)
		argv.push_back(const_cast<char*>(a));
	return execCli(int(argv.size()), argv.data());
}

/**
 * Runs create-map on a fixture in test/data/create-map, overriding the print
 * area options and appending any extra arguments.
 * 
 * An empty print_area or print_area_utm omits the option entirely.
 * 
 * The paths are absolute because the OGR importer passes them to GDAL, which
 * does not know about the Qt search paths set up in initTestCase().
 */
int execCreateMap(const QString& fixture, const QString& output,
                  const QString& print_area = QString(),
                  const QString& print_area_utm = QString(),
                  const QStringList& extra_args = QStringList())
{
	const QString data_dir = QDir(QString::fromUtf8(MAPPER_TEST_SOURCE_DIR))
	                             .absoluteFilePath(QStringLiteral("data"));
	const QString input_path = data_dir + QStringLiteral("/create-map/") + fixture;
	const QString symbols_path = data_dir + QStringLiteral("/export/single-line.xmap");
	const QString crt_path = data_dir + QStringLiteral("/create-map/objects.crt");

	std::vector<std::string> args{
		"mapper", "--cli", "create-map",
		"--input", input_path.toStdString(),
		"--symbols", symbols_path.toStdString(),
		"--crt", crt_path.toStdString(),
		"--output", output.toStdString()
	};
	if (!print_area.isEmpty())
	{
		args.push_back("--print-area");
		args.push_back(print_area.toStdString());
	}
	if (!print_area_utm.isEmpty())
	{
		args.push_back("--print-area-utm");
		args.push_back(print_area_utm.toStdString());
	}
	for (const auto& arg : extra_args)
		args.push_back(arg.toStdString());

	std::vector<char*> argv;
	argv.reserve(args.size());
	for (auto& arg : args)
		argv.push_back(&arg[0]);
	return execCli(int(argv.size()), argv.data());
}

}  // namespace


void CliTest::initTestCase()
{
	doStaticInitializations();

	// Register a Qt search path for test data
	QDir::addSearchPath(QStringLiteral("testdata"),
		QDir(QString::fromUtf8(MAPPER_TEST_SOURCE_DIR))
			.absoluteFilePath(QStringLiteral("data")));
}


void CliTest::testNoSubcommand()
{
	QCOMPARE(execCliFrom({"mapper", "--cli"}), 1);
}


void CliTest::testNonexistingSubcommand()
{
	QCOMPARE(execCliFrom({"mapper", "--cli", "nonexistingsubcommand"}), 1);
}


void CliTest::testExportMissingInput()
{
	QCOMPARE(execCliFrom({"mapper", "--cli", "export", "-o", "out.pdf"}), 1);
}


void CliTest::testExportMissingOutput()
{
	QCOMPARE(execCliFrom({"mapper", "--cli", "export", "-i", "in.omap"}), 1);
}


void CliTest::testExportInvalidInput()
{
	QCOMPARE(execCliFrom({"mapper", "--cli", "export", "-i", "nonexistent.omap", "-o", "out.pdf"}), 1);
}


void CliTest::testExportPdf()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());

	auto input = QString::fromUtf8("testdata:/barrier.omap");
	QVERIFY(QFileInfo::exists(input));

	QString output = temp_dir.path() + QStringLiteral("/output.pdf");
	auto input_bytes = input.toLocal8Bit();
	auto output_bytes = output.toLocal8Bit();

	int result = execCliFrom({
		"mapper", "--cli", "export",
		"-i", input_bytes.constData(),
		"-o", output_bytes.constData(),
		"--full-map"
	});
	QCOMPARE(result, 0);
	QVERIFY(QFile::exists(output));
	QVERIFY(QFileInfo(output).size() > 0);
}


void CliTest::testExportPng()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());

	auto input = QString::fromUtf8("testdata:/barrier.omap");
	QVERIFY(QFileInfo::exists(input));

	QString output = temp_dir.path() + QStringLiteral("/output.png");
	auto input_bytes = input.toLocal8Bit();
	auto output_bytes = output.toLocal8Bit();

	int result = execCliFrom({
		"mapper", "--cli", "export",
		"-i", input_bytes.constData(),
		"-o", output_bytes.constData(),
		"--full-map"
	});
	QCOMPARE(result, 0);
	QVERIFY(QFile::exists(output));
	QVERIFY(QFileInfo(output).size() > 0);
}


void CliTest::testExportFullMapFlag()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());

	auto input = QString::fromUtf8("testdata:/barrier.omap");
	QString output = temp_dir.path() + QStringLiteral("/output.pdf");
	auto input_bytes = input.toLocal8Bit();
	auto output_bytes = output.toLocal8Bit();

	int result = execCliFrom({
		"mapper", "--cli", "export",
		"-i", input_bytes.constData(),
		"-o", output_bytes.constData(),
		"--full-map"
	});
	QCOMPARE(result, 0);
	QVERIFY(QFile::exists(output));
	QVERIFY(QFileInfo(output).size() > 0);
}


void CliTest::testConvertMissingInput()
{
	QCOMPARE(execCliFrom({"mapper", "--cli", "convert", "-o", "out.omap"}), 1);
}


void CliTest::testConvertMissingOutput()
{
	QCOMPARE(execCliFrom({"mapper", "--cli", "convert", "-i", "in.omap"}), 1);
}


void CliTest::testConvertInvalidInput()
{
	QCOMPARE(execCliFrom({"mapper", "--cli", "convert", "-i", "nonexistent.omap", "-o", "out.omap"}), 1);
}


void CliTest::testConvertOmapToXmap()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());

	auto input = QString::fromUtf8("testdata:/barrier.omap");
	QVERIFY(QFileInfo::exists(input));

	QString output = temp_dir.path() + QStringLiteral("/output.xmap");
	auto input_bytes = input.toLocal8Bit();
	auto output_bytes = output.toLocal8Bit();

	int result = execCliFrom({
		"mapper", "--cli", "convert",
		"-i", input_bytes.constData(),
		"-o", output_bytes.constData()
	});
	QCOMPARE(result, 0);
	QVERIFY(QFile::exists(output));
	QVERIFY(QFileInfo(output).size() > 0);
}


void CliTest::testConvertXmapToOmap()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());

	auto input = QString::fromUtf8("testdata:/barrier.omap");
	QVERIFY(QFileInfo::exists(input));

	QString xmap = temp_dir.path() + QStringLiteral("/intermediate.xmap");
	QString omap = temp_dir.path() + QStringLiteral("/output.omap");
	auto input_bytes = input.toLocal8Bit();
	auto xmap_bytes = xmap.toLocal8Bit();
	auto omap_bytes = omap.toLocal8Bit();

	int r1 = execCliFrom({
		"mapper", "--cli", "convert",
		"-i", input_bytes.constData(),
		"-o", xmap_bytes.constData()
	});
	QCOMPARE(r1, 0);
	QVERIFY(QFile::exists(xmap));

	int r2 = execCliFrom({
		"mapper", "--cli", "convert",
		"-i", xmap_bytes.constData(),
		"-o", omap_bytes.constData()
	});
	QCOMPARE(r2, 0);
	QVERIFY(QFile::exists(omap));
	QVERIFY(QFileInfo(omap).size() > 0);
}


void CliTest::testConvertOmapToOcd()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());

	auto input = QString::fromUtf8("testdata:/barrier.omap");
	QVERIFY(QFileInfo::exists(input));

	QString output = temp_dir.path() + QStringLiteral("/output.ocd");
	auto input_bytes = input.toLocal8Bit();
	auto output_bytes = output.toLocal8Bit();

	int result = execCliFrom({
		"mapper", "--cli", "convert",
		"-i", input_bytes.constData(),
		"-o", output_bytes.constData()
	});
	QCOMPARE(result, 0);
	QVERIFY(QFile::exists(output));
	QVERIFY(QFileInfo(output).size() > 0);
}


void CliTest::testConvertWithFormatId()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());

	auto input = QString::fromUtf8("testdata:/barrier.omap");
	QVERIFY(QFileInfo::exists(input));

	QString output = temp_dir.path() + QStringLiteral("/output.xmap");
	auto input_bytes = input.toLocal8Bit();
	auto output_bytes = output.toLocal8Bit();

	int result = execCliFrom({
		"mapper", "--cli", "convert",
		"-i", input_bytes.constData(),
		"-o", output_bytes.constData(),
		"--output-format", "XML"
	});
	QCOMPARE(result, 0);
	QVERIFY(QFile::exists(output));
	QVERIFY(QFileInfo(output).size() > 0);
}


void CliTest::testExportFormatIdOverridesExtension()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());

	auto input = QString::fromUtf8("testdata:/barrier.omap");
	QVERIFY(QFileInfo::exists(input));

	// Write a PNG to a file named .pdf
	QString output = temp_dir.path() + QStringLiteral("/output.pdf");
	auto input_bytes = input.toLocal8Bit();
	auto output_bytes = output.toLocal8Bit();

	int result = execCliFrom({
		"mapper", "--cli", "export",
		"-i", input_bytes.constData(),
		"-o", output_bytes.constData(),
		"--output-format", "png",
		"--full-map"
	});
	QCOMPARE(result, 0);
	QVERIFY(QFile::exists(output));
	QVERIFY(QFileInfo(output).size() > 0);
}


void CliTest::testConvertFormatIdOverridesExtension()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());

	auto input = QString::fromUtf8("testdata:/barrier.omap");
	QVERIFY(QFileInfo::exists(input));

	// Write XML format to a non-native file extension
	QString output = temp_dir.path() + QStringLiteral("/output.foo");
	auto input_bytes = input.toLocal8Bit();
	auto output_bytes = output.toLocal8Bit();

	int result = execCliFrom({
		"mapper", "--cli", "convert",
		"-i", input_bytes.constData(),
		"-o", output_bytes.constData(),
		"--output-format", "XML"
	});
	QCOMPARE(result, 0);
	QVERIFY(QFile::exists(output));
	QVERIFY(QFileInfo(output).size() > 0);
}


void CliTest::testExportRejectsNativeFormatId()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());

	auto input = QString::fromUtf8("testdata:/barrier.omap");
	QVERIFY(QFileInfo::exists(input));

	QString output = temp_dir.path() + QStringLiteral("/output.foo");
	auto input_bytes = input.toLocal8Bit();
	auto output_bytes = output.toLocal8Bit();

	int result = execCliFrom({
		"mapper", "--cli", "export",
		"-i", input_bytes.constData(),
		"-o", output_bytes.constData(),
		"--output-format", "XML"
	});
	QCOMPARE(result, 1);
}


void CliTest::testConvertRejectsUnknownFormatId()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());

	auto input = QString::fromUtf8("testdata:/barrier.omap");
	QVERIFY(QFileInfo::exists(input));

	QString output = temp_dir.path() + QStringLiteral("/output.omap");
	auto input_bytes = input.toLocal8Bit();
	auto output_bytes = output.toLocal8Bit();

	int result = execCliFrom({
		"mapper", "--cli", "convert",
		"-i", input_bytes.constData(),
		"-o", output_bytes.constData(),
		"--output-format", "nonexistingformat"
	});
	QCOMPARE(result, 1);
}


void CliTest::testCreateMapMissingInput()
{
	QCOMPARE(execCliFrom({
		"mapper", "--cli", "create-map",
		"--symbols", "symbols.xmap",
		"--crt", "objects.crt",
		"--output", "out.omap"
	}), 1);
}


void CliTest::testCreateMapMissingSymbols()
{
	QCOMPARE(execCliFrom({
		"mapper", "--cli", "create-map",
		"--input", "data.gpkg",
		"--crt", "objects.crt",
		"--output", "out.omap"
	}), 1);
}


void CliTest::testCreateMapMissingCrt()
{
	QCOMPARE(execCliFrom({
		"mapper", "--cli", "create-map",
		"--input", "data.gpkg",
		"--symbols", "symbols.xmap",
		"--output", "out.omap"
	}), 1);
}


void CliTest::testCreateMapMissingOutput()
{
	QCOMPARE(execCliFrom({
		"mapper", "--cli", "create-map",
		"--input", "data.gpkg",
		"--symbols", "symbols.xmap",
		"--crt", "objects.crt"
	}), 1);
}


void CliTest::testCreateMapInvalidInput()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());

	QString output = temp_dir.path() + QStringLiteral("/out.omap");
	auto output_bytes = output.toLocal8Bit();

	int result = execCliFrom({
		"mapper", "--cli", "create-map",
		"--input", "nonexistent.gpkg",
		"--symbols", "nonexistent.xmap",
		"--crt", "nonexistent.crt",
		"--output", output_bytes.constData()
	});
	QCOMPARE(result, 1);
	QVERIFY(!QFile::exists(output));
}


void CliTest::testCreateMapPrintAreaWrongPartCount()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());
	const QString output = temp_dir.path() + QStringLiteral("/out.omap");

	QCOMPARE(execCreateMap(QStringLiteral("data.geojson"), output, QStringLiteral("1,2,3")), 1);
	QVERIFY(!QFile::exists(output));
}


void CliTest::testCreateMapPrintAreaNonNumeric()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());
	const QString output = temp_dir.path() + QStringLiteral("/out.omap");

	QCOMPARE(execCreateMap(QStringLiteral("data.geojson"), output, QStringLiteral("left,top,37,52")), 1);
	QVERIFY(!QFile::exists(output));
}


void CliTest::testCreateMapPrintAreaNonPositiveSize()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());
	const QString output = temp_dir.path() + QStringLiteral("/out.omap");

	QCOMPARE(execCreateMap(QStringLiteral("data.geojson"), output, QStringLiteral("0,0,0,52")), 1);
	QVERIFY(!QFile::exists(output));
}


void CliTest::testCreateMapWithoutPrintArea()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());
	const QString output = temp_dir.path() + QStringLiteral("/out.omap");

	QCOMPARE(execCreateMap(QStringLiteral("data.geojson"), output, QString()), 0);
	QVERIFY(QFile::exists(output));

	Map map;
	QVERIFY(map.loadFrom(output));
	QVERIFY(!map.hasPrinterConfig());
}


void CliTest::testCreateMapPrintArea()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());
	const QString output = temp_dir.path() + QStringLiteral("/out.omap");

	QCOMPARE(execCreateMap(QStringLiteral("data.geojson"), output, QStringLiteral("-33.46,-18.512,37,52")), 0);
	QVERIFY(QFile::exists(output));

	Map map;
	QVERIFY(map.loadFrom(output));
	QVERIFY(map.hasPrinterConfig());

	const auto print_area = map.printerConfig().print_area;
	QCOMPARE(print_area.left(), -33.46);
	QCOMPARE(print_area.top(), -18.512);
	QCOMPARE(print_area.width(), 37.0);
	QCOMPARE(print_area.height(), 52.0);
}


void CliTest::testCreateMapPrintAreaUtm()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());
	const QString output = temp_dir.path() + QStringLiteral("/out.omap");

	// Top-left (500000, 5649500) to bottom-right (501000, 5648500), i.e. a
	// 1000 m x 1000 m square centred on (500500, 5649000).
	QCOMPARE(execCreateMap(QStringLiteral("utm.geojson"), output, QString(),
	                       QStringLiteral("500000,5649500,501000,5648500")), 0);
	QVERIFY(QFile::exists(output));

	Map map;
	QVERIFY(map.loadFrom(output));
	QVERIFY(map.hasPrinterConfig());

	const auto print_area = map.printerConfig().print_area;
	const auto to_projected = map.getGeoreferencing().mapToProjected();

	// The map is rotated by the grivation, so the centre of the mapped
	// rectangle is the corner-independent invariant to check exactly.
	const auto center = to_projected.map(print_area.center());
	QVERIFY2(qAbs(center.x() - 500500.0) < 0.5, "print area centre easting");
	QVERIFY2(qAbs(center.y() - 5649000.0) < 0.5, "print area centre northing");

	const auto top_left = to_projected.map(QPointF(print_area.left(), print_area.top()));
	const auto bottom_right = to_projected.map(QPointF(print_area.right(), print_area.bottom()));
	QVERIFY2(qAbs((bottom_right.x() - top_left.x()) - 1000.0) < 5.0,
	         "print area width in metres");
	QVERIFY2(qAbs((bottom_right.y() - top_left.y()) + 1000.0) < 5.0,
	         "print area height in metres");
}


void CliTest::testCreateMapPrintAreaUtmNonUtmInput()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());
	const QString output = temp_dir.path() + QStringLiteral("/out.omap");

	// The other fixture is EPSG:4326, so the map gets a synthetic
	// orthographic projected CRS instead of UTM.
	QCOMPARE(execCreateMap(QStringLiteral("data.geojson"), output, QString(),
	                       QStringLiteral("500000,5649500,501000,5648500")), 1);
	QVERIFY(!QFile::exists(output));
}


void CliTest::testCreateMapPrintAreaUtmInvalidFormat()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());
	const QString output = temp_dir.path() + QStringLiteral("/out.omap");

	QCOMPARE(execCreateMap(QStringLiteral("utm.geojson"), output, QString(),
	                       QStringLiteral("500000,5649500,501000")), 1);
	QVERIFY(!QFile::exists(output));
}


void CliTest::testCreateMapPrintAreaUtmInvertedCorners()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());
	const QString output = temp_dir.path() + QStringLiteral("/out.omap");

	QCOMPARE(execCreateMap(QStringLiteral("utm.geojson"), output, QString(),
	                       QStringLiteral("501000,5648500,500000,5649500")), 1);
	QVERIFY(!QFile::exists(output));
}


void CliTest::testCreateMapPrintAreaOptionsExclusive()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());
	const QString output = temp_dir.path() + QStringLiteral("/out.omap");

	QCOMPARE(execCreateMap(QStringLiteral("utm.geojson"), output,
	                       QStringLiteral("0,0,37,52"),
	                       QStringLiteral("500000,5649500,501000,5648500")), 1);
	QVERIFY(!QFile::exists(output));
}


void CliTest::testCreateMapGrivationByDefault()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());
	const QString output = temp_dir.path() + QStringLiteral("/out.omap");

	QCOMPARE(execCreateMap(QStringLiteral("utm.geojson"), output), 0);
	QVERIFY(QFile::exists(output));

	Map map;
	QVERIFY(map.loadFrom(output));

	// The fixture lies east of the central meridian, so the grivation that
	// create-map derives from the grid convergence is non-zero. Without this,
	// testCreateMapNoGrivation would pass even if the option did nothing.
	QVERIFY2(!qIsNull(map.getGeoreferencing().getGrivation()),
	         "the fixture has no grivation to disable");
}


void CliTest::testCreateMapNoGrivation()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());
	const QString output = temp_dir.path() + QStringLiteral("/out.omap");

	QCOMPARE(execCreateMap(QStringLiteral("utm.geojson"), output, QString(), QString(),
	                       {QStringLiteral("--no-grivation")}), 0);
	QVERIFY(QFile::exists(output));

	// Load through an importer so that warnings are observable.
	Map map;
	auto importer = FileFormats.makeImporter(output, map);
	QVERIFY(importer);
	QVERIFY(importer->doImport());
	QVERIFY(!map.calculateExtent().isNull());

	QCOMPARE(map.getGeoreferencing().getGrivation(), 0.0);

	// The written georeferencing has to be self-consistent. Otherwise Mapper
	// adjusts the declination and warns every time the map is opened.
	for (const auto& warning : importer->warnings())
	{
		QVERIFY2(!warning.contains(QStringLiteral("Inconsistent declination")),
		         qPrintable(warning));
	}
}


void CliTest::testCreateMapNoGrivationNonProjected()
{
	QTemporaryDir temp_dir;
	QVERIFY(temp_dir.isValid());
	const QString output = temp_dir.path() + QStringLiteral("/out.omap");

	// Geographic input ends up with an orthographic CRS, where the grivation
	// is zero anyway. The option must not get in the way.
	QCOMPARE(execCreateMap(QStringLiteral("data.geojson"), output, QString(), QString(),
	                       {QStringLiteral("--no-grivation")}), 0);
	QVERIFY(QFile::exists(output));

	Map map;
	QVERIFY(map.loadFrom(output));
	QCOMPARE(map.getGeoreferencing().getGrivation(), 0.0);
	QVERIFY(!map.calculateExtent().isNull());
}


int main(int argc, char** argv)
{
#ifdef Q_OS_LINUX
	// Offscreen avoids failing when no display server is available
	qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
	QApplication app(argc, argv);
	CliTest tc;
	return QTest::qExec(&tc, argc, argv);
}
