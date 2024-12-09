/*
 Copyright (c) 2015 "Novero GmbH" <http://novero.com>

 This file is part of tragediy <https://github.com/NoveroResearch/tragediy>.

 tragediy is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <tragediy/track/LaneArcTile.h>
#include <tragediy/track/LaneLineTile.h>
#include <tragediy/track/LocationTable.h>
#include <tragediy/track/Track.h>
#include <tragediy/util/BoundingBox.h>
#include <tragediy/util/Constants.h>
#include <tragediy/util/Math.h>
#include <tragediy/util/Vector2.h>

#include <tragediy/track/AnkiDriveMap.h>
#include <tragediy/track/AnkiOverdriveMap.h>

#include <boost/program_options.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace po = boost::program_options;
namespace fs = std::filesystem;

std::string trackName_;
boost::filesystem::path pathToAppData_;
std::string mapFileDrive_, mapFileOverdrive_;
std::string prefix_;
std::string tileSize_("full");
bool isOverrideThetaWithZero_ = false;
double rotation_ = 0.0;
double marginNotPrintable_ = 5.0;
double marginOverlap_ = 5.0;

auto handleCommandlineArguments(int argc, char *argv[]) -> po::variables_map
{
	std::string appname{fs::path(argv[0]).filename()};
	po::options_description options("Usage");
	// clang-format off
    options.add_options()
    ("help,h", "produce help message")
    ("prefix,p", po::value<std::string>(), "prefix of output files")
    ("size,s", po::value<std::string>(), "size of tiling (a4-landscape, a3-landscape, a4-portrait, a3-portrait, full)")
    ("track,t", po::value<std::string>(), "name of programmed tragediy tracks (starter, ring)")
    ("appdata,I", po::value<std::string>(), "path to the app data of Anki's android Drive or Overdrive app (e.g. ~/com.anki.drive)")
    ("import-drive,i", po::value<std::string>(), "Anki Drive map file to import from the app data (e.g. IntersecProduction_map.txt or oval32wide_8pc_map.txt)")
    ("import-overdrive,j", po::value<std::string>(), "Anki Overdrive map file to import from the app data (e.g. modular_gunner.txt or modular_capsule.txt)")
    ("rotate,r", po::value<double>(), "rotate imported Anki maps by the given number of degrees")
    ("zero,z", "import Anki Drive map by overriding default rotation with theta=0")
    ;
	// clang-format on
	po::positional_options_description positionalOptions;

	po::variables_map vm;
	try
	{
		po::store(po::command_line_parser(argc, argv).options(options).positional(positionalOptions).run(), vm);

		if (vm.count("help"))
		{
			std::cout << appname << "\n\n";
			std::cout << options << "\n";
			std::exit(EXIT_FAILURE);
		}

		po::notify(vm);
	}
	catch (po::error &err)
	{
		std::cout << err.what() << std::endl;
		std::exit(EXIT_FAILURE);
	}

	if (vm.count("track"))
		trackName_ = vm["track"].as<std::string>();
	else if (vm.count("import-drive"))
		mapFileDrive_ = vm["import-drive"].as<std::string>();
	else if (vm.count("import-overdrive"))
		mapFileOverdrive_ = vm["import-overdrive"].as<std::string>();
	else
	{
		std::cout << "ERROR: Either command-line option --track must be specified or --import-drive/--import-overdrive.\n";
		std::exit(EXIT_FAILURE);
	}

	if (vm.count("import-drive") && vm.count("import-overdrive"))
	{
		std::cout << "ERROR: --import-drive and --import-overdrive exclude each other.\n";
		std::exit(EXIT_FAILURE);
	}

	if (vm.count("import-drive") || vm.count("import-overdrive"))
	{
		if (vm.count("appdata"))
			pathToAppData_ = vm["appdata"].as<std::string>();
		else
			pathToAppData_ = ".";

		if (!boost::filesystem::is_directory(pathToAppData_))
		{
			std::cout << "ERROR: Track repository path is non-existent." << std::endl;
			std::exit(EXIT_FAILURE);
		}
	}

	if (vm.count("rotate"))
		rotation_ = vm["rotate"].as<double>();
	if (vm.count("zero"))
	{
		isOverrideThetaWithZero_ = true;
	}

	return vm;
}

auto setPaperSize(const BoundingBox &bbTrack) -> std::tuple<double, double>
{
	double paperWidth;
	double paperHeight;
	if (tileSize_ == "a3-landscape")
	{
		paperWidth = 420.0;
		paperHeight = 297.0;
	}
	else if (tileSize_ == "a4-landscape")
	{
		paperWidth = 297.0;
		paperHeight = 210.0;
	}
	else if (tileSize_ == "a3-portrait")
	{
		paperWidth = 297.0;
		paperHeight = 420.0;
	}
	else if (tileSize_ == "a4-portrait")
	{
		paperWidth = 210.0;
		paperHeight = 297.0;
	}
	else if (tileSize_ == "letter-landscape")
	{
		paperWidth = 279.4;
		paperHeight = 215.9;
	}
	else if (tileSize_ == "letter-portrait")
	{
		paperWidth = 215.9;
		paperHeight = 279.4;
	}
	else if (tileSize_ == "legal-landscape")
	{
		paperWidth = 355.6;
		paperHeight = 215.9;
	}
	else if (tileSize_ == "legal-portrait")
	{
		paperWidth = 215.9;
		paperHeight = 355.6;
	}
	else
	{
		paperWidth = bbTrack.xMax_ - bbTrack.xMin_;
		paperHeight = bbTrack.yMax_ - bbTrack.yMin_;
	}

	return std::make_tuple(paperWidth, paperHeight);
}

void createCleanSVG(const BoundingBox &bbPrint, const Track &track)
{
	std::stringstream sout;
	sout << prefix_ << "_track_clean.svg";

	std::ofstream fout(sout.str().c_str());

	fout << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n";
	fout << "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 20010904//EN\" \"http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd\">\n";
	fout << "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"" << bbPrint.xMax_ - bbPrint.xMin_ << "mm\" height=\"" << bbPrint.yMax_ - bbPrint.yMin_ << "mm\" viewBox=\"" << bbPrint.xMin_ << " " << bbPrint.yMin_ << " " << bbPrint.xMax_ - bbPrint.xMin_ << " " << bbPrint.yMax_ - bbPrint.yMin_ << "\">\n";

	track.writeToStreamAsSvg(fout, bbPrint);

	fout << "</svg>\n";
}

void createAnnotatedSVG(const BoundingBox &bbPrint, const Track &track, const LocationTable &locationTable)
{
	std::stringstream sout;
	sout << prefix_ << "_track_annotated.svg";

	std::ofstream fout(sout.str().c_str());

	fout << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n";
	fout << "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 20010904//EN\" \"http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd\">\n";
	fout << "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"" << bbPrint.xMax_ - bbPrint.xMin_ << "mm\" height=\"" << bbPrint.yMax_ - bbPrint.yMin_ << "mm\" viewBox=\"" << bbPrint.xMin_ << " " << bbPrint.yMin_ << " " << bbPrint.xMax_ - bbPrint.xMin_ << " " << bbPrint.yMax_ - bbPrint.yMin_ << "\">\n";

	track.writeToStreamAsSvg(fout, bbPrint);
	track.writeAnnotationToStreamAsSvg(fout, bbPrint);
	locationTable.writeToStreamAsSvg(fout);

	fout << "</svg>\n";
}

void createTrackJSON(const BoundingBox &bbPrint, const Track &track)
{
	std::stringstream sout;
	sout << prefix_ << "_track.json";

	std::ofstream fout(sout.str().c_str());

	track.writeToStreamAsJson(fout, bbPrint);
}

void createLocationTableCSV(const BoundingBox &bbPrint, const LocationTable &locationTable)
{
	std::stringstream sout;
	sout << prefix_ << "_location-table.csv";

	std::ofstream fout(sout.str().c_str());

	locationTable.writeToStreamAsCsv(fout);
}

void createLocationTableJSON(const BoundingBox &bbPrint, const LocationTable &locationTable)
{
	std::stringstream sout;
	sout << prefix_ << "_location-table.json";

	std::ofstream fout(sout.str().c_str());

	locationTable.writeToStreamAsJson(fout, bbPrint);
}

void createTiledTrackSVG(const BoundingBox &bbTrack, const Track &track, std::size_t numPapersX, std::size_t numPapersY, double paperWidth, double paperHeight)
{
	for (std::size_t iX = 0; iX < numPapersX; ++iX)
	{
		for (std::size_t iY = 0; iY < numPapersY; ++iY)
		{
			std::stringstream sout;
			sout << prefix_ << "_track_" << iX << "x" << iY << ".svg";

			BoundingBox bbPaper;
			double tmp = 2.0 * marginNotPrintable_ + marginOverlap_;
			bbPaper.xMin_ = bbTrack.xMin_ + (paperWidth - tmp) * iX;
			bbPaper.yMin_ = bbTrack.yMin_ + (paperHeight - tmp) * iY;
			bbPaper.xMax_ = bbPaper.xMin_ + paperWidth;
			bbPaper.yMax_ = bbPaper.yMin_ + paperHeight;

			std::ofstream fout(sout.str().c_str());

			fout << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n";
			fout << "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 20010904//EN\" \"http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd\">\n";
			fout << "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"" << paperWidth << "mm\" height=\"" << paperHeight << "mm\" viewBox=\"" << bbPaper.xMin_ << " " << bbPaper.yMin_ << " " << paperWidth << " " << paperHeight << "\">\n";

			track.writeToStreamAsSvg(fout, bbPaper);

			double widthHelperThick = 2.0;
			fout << "<line x1=\"" << bbPaper.xMin_ + 0.0 << "\" y1=\"" << bbPaper.yMin_ + marginNotPrintable_ - 0.5 * widthHelperThick << "\" x2=\"" << bbPaper.xMin_ + marginNotPrintable_ << "\" y2=\"" << bbPaper.yMin_ + marginNotPrintable_ - 0.5 * widthHelperThick << "\" style=\"stroke:cyan; stroke-width:" << widthHelperThick << ";\"/>\n";
			fout << "<line x1=\"" << bbPaper.xMax_ - marginNotPrintable_ << "\" y1=\"" << bbPaper.yMin_ + marginNotPrintable_ - 0.5 * widthHelperThick << "\" x2=\"" << bbPaper.xMax_ << "\" y2=\"" << bbPaper.yMin_ + marginNotPrintable_ - 0.5 * widthHelperThick << "\" style=\"stroke:cyan; stroke-width:" << widthHelperThick << ";\"/>\n";
			fout << "<line x1=\"" << bbPaper.xMin_ + marginNotPrintable_ - 0.5 * widthHelperThick << "\" y1=\"" << bbPaper.yMin_ + 0.0 << "\" x2=\"" << bbPaper.xMin_ + marginNotPrintable_ - 0.5 * widthHelperThick << "\" y2=\"" << bbPaper.yMin_ + marginNotPrintable_ << "\" style=\"stroke:cyan; stroke-width:" << widthHelperThick << ";\"/>\n";
			fout << "<line x1=\"" << bbPaper.xMin_ + marginNotPrintable_ - 0.5 * widthHelperThick << "\" y1=\"" << bbPaper.yMax_ - marginNotPrintable_ << "\" x2=\"" << bbPaper.xMin_ + marginNotPrintable_ - 0.5 * widthHelperThick << "\" y2=\"" << bbPaper.yMax_ << "\" style=\"stroke:cyan; stroke-width:" << widthHelperThick << ";\"/>\n";
			fout << "<line x1=\"" << bbPaper.xMin_ + 0.0 << "\" y1=\"" << bbPaper.yMax_ - marginNotPrintable_ - marginOverlap_ + 0.5 * widthHelperThick << "\" x2=\"" << bbPaper.xMin_ + marginNotPrintable_ << "\" y2=\"" << bbPaper.yMax_ - marginNotPrintable_ - marginOverlap_ + 0.5 * widthHelperThick << "\" style=\"stroke:cyan; stroke-width:" << widthHelperThick << ";\"/>\n";
			fout << "<line x1=\"" << bbPaper.xMax_ - marginNotPrintable_ << "\" y1=\"" << bbPaper.yMax_ - marginNotPrintable_ - marginOverlap_ + 0.5 * widthHelperThick << "\" x2=\"" << bbPaper.xMax_ << "\" y2=\"" << bbPaper.yMax_ - marginNotPrintable_ - marginOverlap_ + 0.5 * widthHelperThick << "\" style=\"stroke:cyan; stroke-width:" << widthHelperThick << ";\"/>\n";
			fout << "<line x1=\"" << bbPaper.xMax_ - marginNotPrintable_ - marginOverlap_ + 0.5 * widthHelperThick << "\" y1=\"" << bbPaper.yMin_ + 0.0 << "\" x2=\"" << bbPaper.xMax_ - marginNotPrintable_ - marginOverlap_ + 0.5 * widthHelperThick << "\" y2=\"" << bbPaper.yMin_ + marginNotPrintable_ << "\" style=\"stroke:cyan; stroke-width:" << widthHelperThick << ";\"/>\n";
			fout << "<line x1=\"" << bbPaper.xMax_ - marginNotPrintable_ - marginOverlap_ + 0.5 * widthHelperThick << "\" y1=\"" << bbPaper.yMax_ - marginNotPrintable_ << "\" x2=\"" << bbPaper.xMax_ - marginNotPrintable_ - marginOverlap_ + 0.5 * widthHelperThick << "\" y2=\"" << bbPaper.yMax_ << "\" style=\"stroke:cyan; stroke-width:" << widthHelperThick << ";\"/>\n";

			double widthHelperThin = 0.1;
			fout << "<line x1=\"" << bbPaper.xMin_ << "\" y1=\"" << bbPaper.yMin_ + marginNotPrintable_ - 0.5 * widthHelperThin << "\" x2=\"" << bbPaper.xMax_ << "\" y2=\"" << bbPaper.yMin_ + marginNotPrintable_ - 0.5 * widthHelperThin << "\" style=\"stroke:cyan; stroke-dasharray:2,2; stroke-width:" << widthHelperThin << ";\"/>\n";
			fout << "<line x1=\"" << bbPaper.xMin_ + marginNotPrintable_ - 0.5 * widthHelperThin << "\" y1=\"" << bbPaper.yMin_ << "\" x2=\"" << bbPaper.xMin_ + marginNotPrintable_ - 0.5 * widthHelperThin << "\" y2=\"" << bbPaper.yMax_ << "\" style=\"stroke:cyan; stroke-dasharray:2,2; stroke-width:" << widthHelperThin << ";\"/>\n";
			fout << "<line x1=\"" << bbPaper.xMin_ << "\" y1=\"" << bbPaper.yMax_ - marginNotPrintable_ - marginOverlap_ + 0.5 * widthHelperThin << "\" x2=\"" << bbPaper.xMax_ << "\" y2=\"" << bbPaper.yMax_ - marginNotPrintable_ - marginOverlap_ + 0.5 * widthHelperThin << "\" style=\"stroke:cyan; stroke-dasharray:2,2; stroke-width:" << widthHelperThin << ";\"/>\n";
			fout << "<line x1=\"" << bbPaper.xMax_ - marginNotPrintable_ - marginOverlap_ + 0.5 * widthHelperThin << "\" y1=\"" << bbPaper.yMin_ << "\" x2=\"" << bbPaper.xMax_ - marginNotPrintable_ - marginOverlap_ + 0.5 * widthHelperThin << "\" y2=\"" << bbPaper.yMax_ << "\" style=\"stroke:cyan; stroke-dasharray:2,2; stroke-width:" << widthHelperThin << ";\"/>\n";

			double scale = 0.75;
			fout << "<text x=\"" << bbPaper.xMin_ + marginNotPrintable_ + marginOverlap_ + 0.5 * (1 - scale) * marginOverlap_ << "\" y=\"" << bbPaper.yMax_ - marginNotPrintable_ - 0.5 * (1 - scale) * marginOverlap_ << "\" style=\"font-size:" << scale * marginOverlap_ << "; fill:cyan\">" << iX << "x" << iY << "</text>\n";

			fout << "</svg>\n";
		}
	}
}

static fs::path getBasename(const fs::path &path)
{
	return path.stem();
}

int main(int argc, char *argv[])
{
	po::variables_map vm;
	vm = handleCommandlineArguments(argc, argv);

	if (vm.count("prefix"))
		prefix_ = vm["prefix"].as<std::string>();
	else
	{
		if (!trackName_.empty())
		{
			prefix_ = trackName_;
		}
		else if (!mapFileDrive_.empty())
		{
			prefix_ = getBasename(mapFileDrive_);
		}
		else if (!mapFileOverdrive_.empty())
		{
			prefix_ = getBasename(mapFileOverdrive_);
		}
		else
		{
			std::cerr << "ERROR: When importing tracks from Anki app data --prefix is mandatory." << std::endl;
			std::exit(EXIT_FAILURE);
		}
	}

	if (vm.count("size"))
	{
		tileSize_ = vm["size"].as<std::string>();

		if (tileSize_ != "a3-landscape" && tileSize_ != "a4-landscape" && tileSize_ != "a3-portrait" && tileSize_ != "a4-portrait" && tileSize_ != "letter-landscape" && tileSize_ != "legal-landscape" && tileSize_ != "letter-portrait" && tileSize_ != "legal-portrait" && tileSize_ != "full")
		{
			std::cout << "Invalid tile size '" << tileSize_ << "'." << std::endl;
			std::exit(EXIT_FAILURE);
		}
	}

	Track track, borders;

	if (trackName_ == "starter")
		constructStarterTrack(track);
	else if (trackName_ == "ring")
		constructRingTrack(track, borders, 150.0, 220.0, 10, false);
	else if (trackName_ == "" && !mapFileDrive_.empty())
	{
		// Import map file.
		AnkiDriveMap ankiMap;
		try
		{
			ankiMap.loadRacingMap(pathToAppData_, mapFileDrive_.c_str());
			if (isOverrideThetaWithZero_)
			{
				ankiMap.resetTheta();
			}
			ankiMap.convert(track, (rotation_ / 180.0) * pi<double>);
		}
		catch (std::exception &err)
		{
			std::cout << "ERROR: Cannot load Anki Drive map: " << err.what() << std::endl;
			std::exit(EXIT_FAILURE);
		}
	}
	else if (trackName_ == "" && !mapFileOverdrive_.empty())
	{
		// Import map file.
		AnkiOverdriveMap ankiMap;
		try
		{
			ankiMap.loadRacingMap(pathToAppData_, mapFileOverdrive_.c_str());
			ankiMap.convert(track, (rotation_ / 180.0) * pi<double>);
		}
		catch (std::exception &err)
		{
			std::cout << "ERROR: Cannot load Anki Overdrive map: " << err.what() << std::endl;
			std::exit(EXIT_FAILURE);
		}
	}
	else
	{
		std::cout << "ERROR: Invalid track name '" << trackName_ << "'. Track name must be one of 'starter' or 'ring'." << std::endl;
		std::exit(EXIT_FAILURE);
	}

	LocationTable locationTable(track);

	// Determine overall bounding box.
	auto bbTrack = track.adaptCanvas();
	assert(!bbTrack.isEmpty());

	double margin = marginNotPrintable_ + marginOverlap_ + 22.5;
	bbTrack.xMin_ -= margin;
	bbTrack.xMax_ += margin;
	bbTrack.yMin_ -= margin;
	bbTrack.yMax_ += margin;

	double paperWidth;
	double paperHeight;
	std::tie(paperWidth, paperHeight) = setPaperSize(bbTrack);

	double wholeMargin = 2.0 * marginNotPrintable_ + marginOverlap_;
	std::size_t numPapersX = (std::size_t)(std::ceil(((bbTrack.xMax_ - bbTrack.xMin_) - wholeMargin) / (paperWidth - wholeMargin)));
	std::size_t numPapersY = (std::size_t)(std::ceil(((bbTrack.yMax_ - bbTrack.yMin_) - wholeMargin) / (paperHeight - wholeMargin)));

	// It is assumed that in a printout all outer margins are cut off. If the printout is cut differently the bounding box should be adapted here.
	BoundingBox bbPrint;
	bbPrint.xMin_ = bbTrack.xMin_ + marginNotPrintable_;
	bbPrint.yMin_ = bbTrack.yMin_ + marginNotPrintable_;
	bbPrint.xMax_ = bbTrack.xMin_ + numPapersX * (paperWidth - wholeMargin);
	bbPrint.yMax_ = bbTrack.yMin_ + numPapersY * (paperHeight - wholeMargin);

	createCleanSVG(bbPrint, track);

	createAnnotatedSVG(bbPrint, track, locationTable);

	createTrackJSON(bbPrint, track);

	createLocationTableCSV(bbPrint, locationTable);

	createLocationTableJSON(bbPrint, locationTable);

	if (numPapersX > 1 || numPapersY > 1)
		createTiledTrackSVG(bbTrack, track, numPapersX, numPapersY, paperWidth, paperHeight);
}
