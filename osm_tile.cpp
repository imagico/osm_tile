/* ========================================================================
    File: @(#)osm_tile.cpp
   ------------------------------------------------------------------------
    osm_tile OSM file splitter
    Copyright (C) 2013-2014 Christoph Hormann <chris_hormann@gmx.de>
    based on code from Osmium/OSMCoastline 
    Copyright (C) 2012 Jochen Topf <jochen@topf.org>
   ------------------------------------------------------------------------

    This file is part of osm_tile

    osm_tile is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    osm_tile is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with osm_tile.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <iostream>
#include <string>
#include <vector>
#include <getopt.h>

#define OSMIUM_WITH_PBF_INPUT
#define OSMIUM_WITH_XML_INPUT
#define OSMIUM_WITH_PBF_OUTPUT
#define OSMIUM_WITH_XML_OUTPUT

#include <osmium.hpp>
#include <osmium/output/xml.hpp>
#include <osmium/output/pbf.hpp>
#include <osmium/storage/byid/sparse_table.hpp>
#include <osmium/storage/byid/mmap_file.hpp>
#include <osmium/handler/coordinates_for_ways.hpp>

std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems)
{
	std::stringstream ss(s);
	std::string item;
	while(std::getline(ss, item, delim))
	{
		elems.push_back(item);
	}
	return elems;
}

std::vector<std::string> split(const std::string &s, char delim)
{
	std::vector<std::string> elems;
	return split(s, delim, elems);
}

typedef Osmium::Storage::ById::SparseTable<Osmium::OSM::Position> storage_sparsetable_t;
typedef Osmium::Storage::ById::MmapFile<Osmium::OSM::Position> storage_mmap_t;
typedef Osmium::Handler::CoordinatesForWays<storage_sparsetable_t, storage_mmap_t> cfw_handler_t;

class TileHandler : public Osmium::Handler::Base {

	storage_sparsetable_t store_pos;
	storage_mmap_t store_neg;
	cfw_handler_t* handler_cfw;

	std::vector<Osmium::OSM::Bounds> m_Tiles;
	std::vector<std::string> m_Fnms;
	std::vector<Osmium::Output::Handler*> m_Handles;
	std::vector<Osmium::OSMFile*> m_Files;
	size_t m_count_way;
	size_t m_count;

public:

	TileHandler(const std::vector<Osmium::OSM::Bounds>& Tiles, const std::vector<std::string>& Files)
		: Base(),
			m_Tiles(Tiles),
			m_Fnms(Files) { 

		handler_cfw = new cfw_handler_t(store_pos, store_neg);

		m_count = 0;
		m_count_way = 0;
	}


	~TileHandler() {
		delete handler_cfw;
	}

	void init(Osmium::OSM::Meta& meta) {
		handler_cfw->init(meta);

		std::vector<Osmium::OSM::Bounds>::iterator iter_bbox;
		std::vector<std::string>::iterator iter_fnm;

		for (iter_bbox = m_Tiles.begin(), iter_fnm = m_Fnms.begin(); iter_bbox != m_Tiles.end(); ++iter_bbox, ++iter_fnm)
		{
			Osmium::OSMFile* f = new Osmium::OSMFile(*iter_fnm);
			m_Files.push_back(f);
			Osmium::Output::Handler* h = new Osmium::Output::Handler(*f);
			h->set_debug_level(2);
			h->set_generator("osm_tile");
			h->init(meta);
			m_Handles.push_back(h);
		}
	}

	void before_nodes() const {
		for (std::vector<Osmium::Output::Handler*>::const_iterator iter_h = m_Handles.begin(); iter_h != m_Handles.end(); ++iter_h)
			(*iter_h)->before_nodes();
	}

	void node(const shared_ptr<Osmium::OSM::Node>& node) {
		handler_cfw->node(node);

		std::vector<Osmium::OSM::Bounds>::iterator iter_bbox;
		std::vector<Osmium::Output::Handler*>::iterator iter_h;

		for (iter_bbox = m_Tiles.begin(), iter_h = m_Handles.begin(); iter_bbox != m_Tiles.end(); ++iter_bbox, ++iter_h)
		{
			if ((node->lon() > iter_bbox->bottom_left().lon()) && (node->lon() < iter_bbox->top_right().lon()))
				if ((node->lat() > iter_bbox->bottom_left().lat()) && (node->lat() < iter_bbox->top_right().lat()))
					(*iter_h)->node(node);
		}

		m_count++;
		if ((m_count % 10000) == 0)
		{
			fprintf(stderr, "         \r %d nodes...", m_count);
		}
	}

	void after_nodes() {
		std::cerr << "Memory used for node coordinates storage (approximate):\n  for positive IDs: "
							<< store_pos.used_memory() / (1024 * 1024)
							<< " MiB\n  for negative IDs: "
							<< store_neg.used_memory() / (1024 * 1024)
							<< " MiB\n";
		handler_cfw->after_nodes();
		for (std::vector<Osmium::Output::Handler*>::const_iterator iter_h = m_Handles.begin(); iter_h != m_Handles.end(); ++iter_h)
			(*iter_h)->after_nodes();
	}

	void before_ways() const {
		for (std::vector<Osmium::Output::Handler*>::const_iterator iter_h = m_Handles.begin(); iter_h != m_Handles.end(); ++iter_h)
			(*iter_h)->before_ways();
	}

	void way(const shared_ptr<Osmium::OSM::Way>& way) {
		handler_cfw->way(way);

		Osmium::OSM::Bounds wbounds;

		for (Osmium::OSM::WayNodeList::const_iterator iter = way->nodes().begin(); iter != way->nodes().end(); ++iter)
		{
			wbounds.extend(iter->position());
		}

		std::vector<Osmium::OSM::Bounds>::iterator iter_bbox;
		std::vector<Osmium::Output::Handler*>::iterator iter_h;

		for (iter_bbox = m_Tiles.begin(), iter_h = m_Handles.begin(); iter_bbox != m_Tiles.end(); ++iter_bbox, ++iter_h)
		{
			if ((wbounds.top_right().lon() > iter_bbox->bottom_left().lon()) && (wbounds.bottom_left().lon() < iter_bbox->top_right().lon()))
				if ((wbounds.top_right().lat() > iter_bbox->bottom_left().lat()) && (wbounds.bottom_left().lat() < iter_bbox->top_right().lat()))
					(*iter_h)->way(way);
		}

		m_count_way++;
		if ((m_count_way % 1000) == 0)
		{
			fprintf(stderr, "         \r %d ways...", m_count_way);
		}
	}

	void after_ways() {
		fprintf(stderr, "         \rprocessed %ld nodes, %ld ways.\n", m_count, m_count_way);

		int i=0;

		for (std::vector<Osmium::Output::Handler*>::iterator iter_h = m_Handles.begin(); iter_h != m_Handles.end(); ++iter_h)
		{
			i++;
			fprintf(stderr, "finalizing output %d/%d...\n", i, m_Handles.size());

			(*iter_h)->after_ways();
			(*iter_h)->final();
			delete (*iter_h);
		}

		for (std::vector<Osmium::OSMFile*>::iterator iter_f = m_Files.begin(); iter_f != m_Files.end(); ++iter_f)
			delete (*iter_f);

		throw Osmium::Handler::StopReading();
	}
};


void print_help() {
	std::cout << "osm_tile [OPTIONS] OSMFILE\n"
						<< "\nOptions:\n"
						<< "  -h, --help           - This help message\n"
						<< "  -d, --debug          - Enable debugging output\n"
						<< "  -o, --output=OSMFILES - Where to write output (file list separated by ':')\n"
						<< "  -b, --bounds=BOUNDS - Bounds of output files (list separated by ':', coordinates by ',')\n"
						<< "\n";
}

int main(int argc, char* argv[]) {
	bool debug = false;
	std::string output_filenames;
	std::string output_bounds;

	static struct option long_options[] = {
		{"debug",        no_argument, 0, 'd'},
		{"help",         no_argument, 0, 'h'},
		{"output", required_argument, 0, 'o'},
		{"bounds", required_argument, 0, 'b'},
		{0, 0, 0, 0}
	};

	while (1) {
		int c = getopt_long(argc, argv, "dho:b:", long_options, 0);
		if (c == -1)
			break;

		switch (c) {
			case 'd':
				debug = true;
				break;
			case 'h':
				print_help();
				exit(0);
			case 'o':
				output_filenames = optarg;
				break;
			case 'b':
				output_bounds = optarg;
				break;
			default:
				exit(1);
		}
	}

	if (output_filenames.empty()) {
		std::cerr << "Missing -o/--output=OSMFILES option\n";
		exit(1);
	}

	if (output_bounds.empty()) {
		std::cerr << "Missing -b/--bounds=BOUNDS option\n";
		exit(1);
	}

	if (optind != argc - 1) {
		std::cerr << "Usage: osm_tile [OPTIONS] OSMFILE\n";
		exit(1);
	}

	const std::vector<std::string> FnmsOut = split(output_filenames, ':');
	const std::vector<std::string> SBounds = split(output_bounds, ':');
	std::vector<Osmium::OSM::Bounds> Bounds;

	if (FnmsOut.size() != SBounds.size()) {
		std::cerr << "Bounds and filename list have different length\n";
		exit(1);
	}

	std::cerr << "Tiling into " << FnmsOut.size() << " tiles...\n";

	for (int i = 0; i < FnmsOut.size(); i++)
	{
		std::vector<std::string> bcoords = split(SBounds[i], ',');
		Osmium::OSM::Bounds b;
		Osmium::OSM::Position p1(atof(bcoords[0].c_str()), atof(bcoords[1].c_str()));
		b.extend(p1);
		Osmium::OSM::Position p2(atof(bcoords[2].c_str()), atof(bcoords[3].c_str()));
		b.extend(p2);

		Bounds.push_back(b);
	}

	try {
		try {
			Osmium::OSMFile infile(argv[optind]);
			TileHandler handler(Bounds, FnmsOut);
			Osmium::Input::read(infile, handler);
		} catch (Osmium::OSMFile::IOError) {
			std::cerr << "Can not open input file '" << argv[optind] << "'\n";
			exit(1);
		}
	} catch (Osmium::OSMFile::IOError) {
		std::cerr << "Can not open output files '" << output_filenames << "'\n";
		exit(1);
	}
}
