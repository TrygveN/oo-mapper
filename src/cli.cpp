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

#include "cli.h"

#include <cstdio>
#include <memory>
#include <utility>

#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QLatin1String>
#include <QPainter>
#include <QStringList>
#include <QTextStream>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_printer.h"
#include "core/objects/symbol_rule_set.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"

#ifdef QT_PRINTSUPPORT_LIB
#include <QPageSize>
#include <QPrinter>
#include <QPrinterInfo>
#include "core/map_printer.h"
#endif


namespace OpenOrienteering {

namespace {


/**
 * Creates the exporter for the given output path.
 * 
 * The exporter is resolved and created before the map is loaded so that
 * clients can query the exporter (e.g. for Exporter::needsObjectExtents())
 * before the map is populated.
 */
std::unique_ptr<Exporter> makeFileFormatExporter(const QString& output_path, const Map& map, const QString& format_id)
{
	const FileFormat* format = nullptr;
	if (!format_id.isEmpty())
		format = FileFormats.findFormat(format_id.toLatin1().constData());
	else
		format = FileFormats.findFormatForFilename(output_path, &FileFormat::supportsWriting);

	if (!format)
		return nullptr;

	return format->makeExporter(output_path, &map, nullptr);
}


/**
 * Parses four comma-separated numbers into out[0..3].
 * 
 * On failure, a message naming option_name and showing expected form is
 * printed to stderr and false is returned.
 */
bool parseFourNumbers(const QString& option_name, const QString& expected, const QString& value, qreal* out)
{
	const auto report_error = [&option_name, &expected, &value]() {
		fprintf(stderr, "Error: %s expects four comma-separated numbers '%s', got '%s'.\n",
		        qPrintable(option_name), qPrintable(expected), qPrintable(value));
	};

	const auto parts = value.split(QLatin1Char(','));
	if (parts.size() != 4)
	{
		report_error();
		return false;
	}

	for (int i = 0; i < 4; ++i)
	{
		bool ok = false;
		out[i] = parts[i].trimmed().toDouble(&ok);
		if (!ok)
		{
			report_error();
			return false;
		}
	}
	return true;
}



#ifdef QT_PRINTSUPPORT_LIB

bool exportViaPdf(const QString& output_path, Map& map, const QRectF& print_area, int dpi = 300)
{
	if (print_area.isEmpty())
	{
		fprintf(stderr, "Error: map has no extent\n");
		return false;
	}

	MapPrinter printer(map, nullptr);
	printer.setTarget(MapPrinter::pdfTarget());
	printer.setPrintArea(print_area);
	printer.setCustomPageSize(print_area.size());
	printer.setResolution(dpi);

	auto qprinter = printer.makePrinter();
	if (!qprinter)
	{
		fprintf(stderr, "Error: failed to create PDF printer.\n");
		return false;
	}

	qprinter->setOutputFileName(output_path);

	// setOutputFileName may reset the page size, re-apply it
	qprinter->setFullPage(true);
	qprinter->setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
	qprinter->setPaperSize(printer.getPrintAreaPaperSize(), QPrinter::Millimeter);

	QPainter painter(qprinter.get());
	printer.drawPage(&painter, printer.getPrintArea());
	painter.end();
	return true;
}


bool exportViaImage(const QString& output_path, Map& map, const QRectF& print_area, int dpi = 300, const char* image_format = nullptr)
{
	if (print_area.isEmpty())
	{
		fprintf(stderr, "Error: map has no extent\n");
		return false;
	}

	MapPrinter printer(map, nullptr);
	printer.setTarget(MapPrinter::imageTarget());
	printer.setPrintArea(print_area);
	printer.setResolution(dpi);

	auto const& options = printer.getOptions();
	qreal pixel_per_mm = options.resolution / 25.4;
	QSizeF mm_size = printer.getPrintAreaPaperSize();
	int w = qMax(1, qRound(mm_size.width() * pixel_per_mm));
	int h = qMax(1, qRound(mm_size.height() * pixel_per_mm));

	QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
	if (image.isNull())
	{
		fprintf(stderr, "Error: not enough memory for image of size %dx%d\n", w, h);
		return false;
	}
	image.fill(Qt::white);
	image.setDotsPerMeterX(qRound(pixel_per_mm * 1000));
	image.setDotsPerMeterY(qRound(pixel_per_mm * 1000));

	QPainter p(&image);
	printer.drawPage(&p, printer.getPrintArea(), &image);
	p.end();

	if (!image.save(output_path, image_format))
	{
		fprintf(stderr, "Error: failed to save image to '%s'\n", qPrintable(output_path));
		return false;
	}
	return true;
}

#endif


int runExport(const QStringList& sub_args)
{
	QCommandLineParser parser;
	parser.setApplicationDescription(QStringLiteral("Export map to printable and GIS formats"));

	QCommandLineOption input_option(
		QStringList{QStringLiteral("i"), QStringLiteral("input")},
		QStringLiteral("Input map file."),
		QStringLiteral("path"));
	parser.addOption(input_option);

	QCommandLineOption output_option(
		QStringList{QStringLiteral("o"), QStringLiteral("output")},
		QStringLiteral("Output file path."),
		QStringLiteral("path"));
	parser.addOption(output_option);

	QCommandLineOption format_option(
		QStringLiteral("output-format"),
		QStringLiteral("Output format ID (e.g. pdf, png, jpg)."),
		QStringLiteral("id"));
	parser.addOption(format_option);

	QCommandLineOption full_map_option(QStringLiteral("full-map"), QStringLiteral("Export the full map extent instead of the saved print area."));
	parser.addOption(full_map_option);

	QCommandLineOption dpi_option(
		QStringLiteral("dpi"),
		QStringLiteral("Output resolution in DPI (default: 300)."),
		QStringLiteral("dpi"),
		QStringLiteral("300"));
	parser.addOption(dpi_option);

	parser.addHelpOption();

	// QCommandLineParser requires the program name as first argument
	QStringList cli_args = {QStringLiteral("mapper")};
	cli_args.append(sub_args);

	if (!parser.parse(cli_args))
	{
		fprintf(stderr, "Error: %s\n", qPrintable(parser.errorText()));
		return 1;
	}

	if (parser.isSet(QStringLiteral("help")))
	{
		fprintf(stderr, "%s", qPrintable(parser.helpText()));
		return 0;
	}

	if (!parser.isSet(input_option))
	{
		fprintf(stderr, "Error: --input / -i is required.\n");
		return 1;
	}
	QString input_path = parser.value(input_option);

	if (!parser.isSet(output_option))
	{
		fprintf(stderr, "Error: --output / -o is required.\n");
		return 1;
	}
	QString output_path = parser.value(output_option);

	QString format_id = parser.value(format_option);

	int dpi = parser.value(dpi_option).toInt();

	Map map;
	if (!map.loadFrom(input_path))
	{
		fprintf(stderr, "Error: failed to load map from '%s'\n", qPrintable(input_path));
		return 1;
	}

	QRectF print_area = parser.isSet(full_map_option)
		? map.calculateExtent()
		: map.printerConfig().print_area;
	if (print_area.isEmpty())
	{
		fprintf(stderr, "Error: map has no content in the selected print area.\n");
		return 1;
	}

	// Determine the format key: use format_id if set, otherwise the file extension
	QString format_key = format_id.isEmpty() ? QFileInfo(output_path).suffix().toLower() : format_id.toLower();

	// Try MapPrinter for PDF and image formats (always, regardless of format_id)
#ifdef QT_PRINTSUPPORT_LIB
	if (format_key == QStringLiteral("pdf"))
	{
		if (exportViaPdf(output_path, map, print_area, dpi))
			return 0;
		fprintf(stderr, "Error: PDF export failed.\n");
		return 1;
	}

	if (QImageWriter::supportedImageFormats().contains(format_key.toLatin1()))
	{
		// Pass explicit format when format_id is set, so QImage::save
		// uses the requested format regardless of file extension.
		QByteArray format_key_latin;
		const char* img_fmt = nullptr;
		if (!format_id.isEmpty())
		{
			format_key_latin = format_key.toLatin1();
			img_fmt = format_key_latin.constData();
		}
		if (exportViaImage(output_path, map, print_area, dpi, img_fmt))
			return 0;
		fprintf(stderr, "Error: image export failed.\n");
		return 1;
	}
#endif

	fprintf(stderr, "Error: no exporter found for '%s'.\n", qPrintable(output_path));
	return 1;
}

int runConvert(const QStringList& sub_args)
{
	QCommandLineParser parser;
	parser.setApplicationDescription(QStringLiteral("Convert between orienteering map formats"));

	QCommandLineOption input_option(
		QStringList{QStringLiteral("i"), QStringLiteral("input")},
		QStringLiteral("Input map file."),
		QStringLiteral("path"));
	parser.addOption(input_option);

	QCommandLineOption output_option(
		QStringList{QStringLiteral("o"), QStringLiteral("output")},
		QStringLiteral("Output file path."),
		QStringLiteral("path"));
	parser.addOption(output_option);

	QCommandLineOption format_option(
		QStringLiteral("output-format"),
		QStringLiteral("Output format ID (e.g. XML, OCD, OCD12)."),
		QStringLiteral("id"),
		QString{});
	parser.addOption(format_option);

	parser.addHelpOption();

	QStringList cli_args = {QStringLiteral("mapper")};
	cli_args.append(sub_args);

	if (!parser.parse(cli_args))
	{
		fprintf(stderr, "Error: %s\n", qPrintable(parser.errorText()));
		return 1;
	}

	if (parser.isSet(QStringLiteral("help")))
	{
		fprintf(stderr, "%s", qPrintable(parser.helpText()));
		return 0;
	}

	if (!parser.isSet(input_option))
	{
		fprintf(stderr, "Error: --input / -i is required.\n");
		return 1;
	}
	QString input_path = parser.value(input_option);

	if (!parser.isSet(output_option))
	{
		fprintf(stderr, "Error: --output / -o is required.\n");
		return 1;
	}
	QString output_path = parser.value(output_option);

	QString format_id = parser.value(format_option);

	Map map;

	// Resolve the exporter before loading: the exporter tells us whether it
	// needs the objects' extents. A map which is only converted and never
	// drawn does not need the renderables the extents are derived from, and
	// building them for a large map takes a lot of time and memory.
	auto exporter = makeFileFormatExporter(output_path, map, format_id);
	if (!exporter)
	{
		fprintf(stderr, "Error: no exporter found for '%s'.\n", qPrintable(output_path));
		return 1;
	}
	map.setRenderablesEnabled(exporter->needsObjectExtents());

	auto importer = FileFormats.makeImporter(input_path, map);
	if (!importer)
	{
		fprintf(stderr, "Error: no importer found for '%s'.\n", qPrintable(input_path));
		return 1;
	}
	if (!importer->doImport())
	{
		fprintf(stderr, "Error: failed to load map from '%s'\n", qPrintable(input_path));
		for (const auto& warning : importer->warnings())
			fprintf(stderr, "  %s\n", qPrintable(warning));
		return 1;
	}

	if (exporter->doExport())
		return 0;

	fprintf(stderr, "Error: failed to write '%s'.\n", qPrintable(output_path));
	for (const auto& warning : exporter->warnings())
		fprintf(stderr, "  %s\n", qPrintable(warning));
	return 1;
}

int runCreateMap(const QStringList& sub_args)
{
	QCommandLineParser parser;
	parser.setApplicationDescription(
		QStringLiteral("Create an OMAP from GeoPackage data, a symbol set, and a CRT"));

	QCommandLineOption input_option(
		QStringList{QStringLiteral("i"), QStringLiteral("input")},
		QStringLiteral("Input GeoPackage file."),
		QStringLiteral("path"));
	parser.addOption(input_option);

	QCommandLineOption symbols_option(
		QStringList{QStringLiteral("s"), QStringLiteral("symbols")},
		QStringLiteral("Input Mapper symbol set (.xmap)."),
		QStringLiteral("path"));
	parser.addOption(symbols_option);

	QCommandLineOption crt_option(
		QStringList{QStringLiteral("c"), QStringLiteral("crt")},
		QStringLiteral("Object-to-symbol cross-reference table (.crt)."),
		QStringLiteral("path"));
	parser.addOption(crt_option);

	QCommandLineOption output_option(
		QStringList{QStringLiteral("o"), QStringLiteral("output")},
		QStringLiteral("Output Mapper map (.omap)."),
		QStringLiteral("path"));
	parser.addOption(output_option);

	QCommandLineOption scale_option(
		QStringLiteral("scale"),
		QStringLiteral("Map scale denominator (e.g. 15000 or 1:15000; default: 15000)."),
		QStringLiteral("scale"),
		QStringLiteral("15000"));
	parser.addOption(scale_option);

	QCommandLineOption print_area_option(
		QStringList{QStringLiteral("p"), QStringLiteral("print-area")},
		QStringLiteral("Print area as 'left,top,width,height' in millimetres at map scale."),
		QStringLiteral("rect"));
	parser.addOption(print_area_option);

	QCommandLineOption print_area_utm_option(
		QStringLiteral("print-area-utm"),
		QStringLiteral("Print area in UTM metres as 'west,north,east,south': top-left and bottom-right corner."),
		QStringLiteral("rect"));
	parser.addOption(print_area_utm_option);

	QCommandLineOption no_grivation_option(
		QStringLiteral("no-grivation"),
		QStringLiteral("Do not derive the grivation from the grid convergence; the map stays unrotated."));
	parser.addOption(no_grivation_option);

	parser.addHelpOption();

	// QCommandLineParser requires the program name as first argument
	QStringList cli_args = {QStringLiteral("mapper")};
	cli_args.append(sub_args);

	if (!parser.parse(cli_args))
	{
		fprintf(stderr, "Error: %s\n", qPrintable(parser.errorText()));
		return 1;
	}

	if (parser.isSet(QStringLiteral("help")))
	{
		fprintf(stderr, "%s", qPrintable(parser.helpText()));
		return 0;
	}

	if (!parser.isSet(input_option))
	{
		fprintf(stderr, "Error: --input / -i is required.\n");
		return 1;
	}
	if (!parser.isSet(symbols_option))
	{
		fprintf(stderr, "Error: --symbols / -s is required.\n");
		return 1;
	}
	if (!parser.isSet(crt_option))
	{
		fprintf(stderr, "Error: --crt / -c is required.\n");
		return 1;
	}
	if (!parser.isSet(output_option))
	{
		fprintf(stderr, "Error: --output / -o is required.\n");
		return 1;
	}

	const QString input_path = parser.value(input_option);
	const QString symbols_path = parser.value(symbols_option);
	const QString crt_path = parser.value(crt_option);
	const QString output_path = parser.value(output_option);
	QString scale_text = parser.value(scale_option).trimmed();
	if (scale_text.startsWith(QStringLiteral("1:")))
		scale_text.remove(0, 2);
	bool scale_ok = false;
	const auto scale_denominator = scale_text.toUInt(&scale_ok);
	if (!scale_ok || scale_denominator == 0)
	{
		fprintf(stderr, "Error: invalid map scale '%s'; expected a positive denominator such as 15000 or 1:15000.\n",
		        qPrintable(parser.value(scale_option)));
		return 1;
	}

	if (parser.isSet(print_area_option) && parser.isSet(print_area_utm_option))
	{
		fprintf(stderr, "Error: --print-area and --print-area-utm are mutually exclusive.\n");
		return 1;
	}

	// The print area is expressed in the map's own coordinates: millimetres
	// on the printed map at the map scale, the same units as the print_area
	// element of a saved map.
	QRectF print_area;
	if (parser.isSet(print_area_option))
	{
		const auto rect_text = parser.value(print_area_option);
		qreal values[4] = {};
		if (!parseFourNumbers(QStringLiteral("--print-area"),
		                      QStringLiteral("left,top,width,height"), rect_text, values))
			return 1;

		if (values[2] <= 0.0 || values[3] <= 0.0)
		{
			fprintf(stderr, "Error: --print-area width and height must be greater than zero.\n");
			return 1;
		}
		print_area = QRectF(values[0], values[1], values[2], values[3]);
	}

	// The UTM print area is a rectangle in projected coordinates, given by its
	// top-left and bottom-right corner. It can only be converted to the map's
	// coordinates once the input's georeferencing is known, so this stores the
	// values for later.
	bool has_utm_print_area = false;
	qreal utm_area[4] = {};
	if (parser.isSet(print_area_utm_option))
	{
		const auto rect_text = parser.value(print_area_utm_option);
		if (!parseFourNumbers(QStringLiteral("--print-area-utm"),
		                      QStringLiteral("west,north,east,south"), rect_text, utm_area))
			return 1;

		if (utm_area[0] >= utm_area[2] || utm_area[3] >= utm_area[1])
		{
			fprintf(stderr, "Error: --print-area-utm needs a top-left corner "
			        "north-west of the bottom-right corner, got '%s'.\n",
			        qPrintable(rect_text));
			return 1;
		}
		has_utm_print_area = true;
	}

	// Import the target symbol set without importing its objects. This keeps
	// the symbol pointers used by SymbolRuleSet::loadCrt() associated with the
	// target symbol map until the rules are applied.
	Map symbol_map;
	symbol_map.setRenderablesEnabled(false);
	auto symbol_importer = FileFormats.makeImporter(symbols_path, symbol_map);
	if (!symbol_importer)
	{
		fprintf(stderr, "Error: no importer found for symbol file '%s'.\n",
		        qPrintable(symbols_path));
		return 1;
	}
	symbol_importer->setLoadSymbolsOnly(true);
	if (!symbol_importer->doImport())
	{
		fprintf(stderr, "Error: failed to load symbol file '%s'.\n",
		        qPrintable(symbols_path));
		for (const auto& warning : symbol_importer->warnings())
			fprintf(stderr, "  %s\n", qPrintable(warning));
		return 1;
	}

	// Import the GeoPackage through the existing OGR importer. In particular,
	// this retains the importer's handling of the source CRS/georeferencing.
	Map map;
	map.setScaleDenominator(scale_denominator);

	// Resolve the exporter before importing: the exporter tells us whether it
	// needs the objects' extents. The map created here is only written out,
	// never drawn, so unless the exporter needs the extents we skip the
	// renderables they are derived from: building them for every object of a
	// large dataset needs a multiple of the memory of the map itself.
	auto exporter = FileFormats.makeExporter(output_path, &map, nullptr);
	if (!exporter)
	{
		fprintf(stderr, "Error: no exporter found for '%s'.\n", qPrintable(output_path));
		return 1;
	}
	map.setRenderablesEnabled(exporter->needsObjectExtents());

	auto object_importer = FileFormats.makeImporter(input_path, map);
	if (!object_importer)
	{
		fprintf(stderr, "Error: no importer found for GeoPackage '%s'.\n",
		        qPrintable(input_path));
		return 1;
	}
	if (parser.isSet(no_grivation_option))
		object_importer->setOption(QStringLiteral("Update grivation"), false);
	if (!object_importer->doImport())
	{
		fprintf(stderr, "Error: failed to import GeoPackage '%s'.\n",
		        qPrintable(input_path));
		for (const auto& warning : object_importer->warnings())
			fprintf(stderr, "  %s\n", qPrintable(warning));
		return 1;
	}

	if (has_utm_print_area)
	{
		const auto& georeferencing = map.getGeoreferencing();
		const auto projected_spec = georeferencing.getProjectedCRSSpec();
		if (georeferencing.getState() != Georeferencing::Geospatial
		    || georeferencing.isGeographic()
		    || !projected_spec.contains(QLatin1String("+proj=utm")))
		{
			const auto crs = projected_spec.isEmpty() ? QStringLiteral("no projected CRS")
			                                          : projected_spec;
			fprintf(stderr, "Error: --print-area-utm requires UTM-referenced input, "
			        "but the map uses '%s'.\n", qPrintable(crs));
			return 1;
		}

		// The transform is rotated by the grivation, so the print area is the
		// bounding box of all four projected corners: print_area is always
		// aligned with the map's axes and has to contain the whole rectangle.
		const auto to_map = georeferencing.projectedToMap();
		const auto corner = [&to_map](qreal easting, qreal northing) {
			return to_map.map(QPointF(easting, northing));
		};
		const auto west = utm_area[0];
		const auto north = utm_area[1];
		const auto east = utm_area[2];
		const auto south = utm_area[3];

		print_area = QRectF(corner(west, north), corner(east, south)).normalized()
		             .united(QRectF(corner(east, north), corner(west, south)).normalized());
	}

	QFile crt_file(crt_path);
	if (!crt_file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		fprintf(stderr, "Error: failed to open CRT '%s'.\n", qPrintable(crt_path));
		return 1;
	}

	QTextStream crt_stream(&crt_file);
	auto rules = SymbolRuleSet::loadCrt(crt_stream, symbol_map);
	if (crt_stream.status() != QTextStream::Ok)
	{
		fprintf(stderr, "Error: failed to read CRT '%s'.\n", qPrintable(crt_path));
		return 1;
	}
	rules.recognizeSymbolPatterns(map);
	if (rules.findDuplicateSymbolPattern())
	{
		fprintf(stderr, "Error: CRT contains conflicting symbol patterns.\n");
		return 1;
	}

	// Import only the symbols selected by the CRT and replace the temporary
	// OGR symbols. Unused imported colors/symbols are removed afterwards.
	std::move(rules).apply(map, symbol_map,
	                       SymbolRuleSet::RemoveUnusedSymbols
	                       | SymbolRuleSet::RemoveUnusedColors);
	map.setSymbolSetId(symbol_map.symbolSetId());

	// Setting the printer config makes the exporter write the print element
	// into the map, so that a later export uses this area by default.
	if (!print_area.isEmpty())
	{
		MapPrinterConfig printer_config = map.printerConfig();
		printer_config.print_area = print_area;
		map.setPrinterConfig(printer_config);
	}

	if (!exporter->doExport())
	{
		fprintf(stderr, "Error: failed to write '%s'.\n", qPrintable(output_path));
		for (const auto& warning : exporter->warnings())
			fprintf(stderr, "  %s\n", qPrintable(warning));
		return 1;
	}

	return 0;
}

int runListFileFormats(const QStringList& sub_args)
{
	fprintf(stdout, "The following types are avaliable:\n");
	for (auto format : FileFormats.formats()) {
		fprintf(stdout, "  '%s'\t(%s)\n", format->id(), format->description().toLocal8Bit().data());
	}
	return 0;
}


}  // namespace


struct SubCommand {
	const char* name;
	const char* description;
	int (*handler)(const QStringList&);
};

const SubCommand sub_commands[] = {
	{ "export",          "Export map to printable and image formats", runExport },
	{ "convert",         "Convert between orienteering map formats",  runConvert },
	{ "create-map",      "Create an OMAP from GeoPackage data and a CRT", runCreateMap },
	{ "listFileFormats", "List all available file formats",           runListFileFormats },
};


int execCli(int argc, char** argv)
{
	if (argc < 3)
	{
		fprintf(stderr, "Error: no subcommand specified.\n");
		return 1;
	}

	QString subcommand = QString::fromLocal8Bit(argv[2]);

	QStringList sub_args;
	for (int i = 3; i < argc; ++i)
		sub_args << QString::fromLocal8Bit(argv[i]);

	for (const auto& cmd : sub_commands)
	{
		if (subcommand == QLatin1String(cmd.name))
			return cmd.handler(sub_args);
	}

	fprintf(stderr, "Error: unknown subcommand '%s'.\n\n", qPrintable(subcommand));
	fprintf(stderr, "Available subcommands:\n");
	for (const auto& cmd : sub_commands)
		fprintf(stderr, "  %-16s %s\n", cmd.name, cmd.description);
	return 1;
}


}  // namespace OpenOrienteering
