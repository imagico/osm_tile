/* ========================================================================
    File: @(#)osmium_tile.cpp
   ------------------------------------------------------------------------
    osmium_tile OSM file splitter
    Copyright (C) 2016 Christoph Hormann <chris_hormann@gmx.de>
    based on osmcoastline_filter
    Copyright 2012-2016 Jochen Topf <jochen@topf.org>
    

    osmium_tile is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    osmium_tile is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with osmium_tile.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <getopt.h>
#include <iostream>
#include <string>
#include <vector>

#include <osmium/index/map/all.hpp>
#include <osmium/handler/node_locations_for_ways.hpp>
#include <osmium/visitor.hpp>

#include <osmium/io/any_input.hpp>
#include <osmium/io/input_iterator.hpp>
#include <osmium/io/output_iterator.hpp>
#include <osmium/io/pbf_output.hpp>
#include <osmium/io/xml_output.hpp>
#include <osmium/handler.hpp>
#include <osmium/osm/entity_bits.hpp>
#include <osmium/util/memory.hpp>
#include <osmium/util/verbose_output.hpp>


typedef osmium::index::map::Map<osmium::unsigned_object_id_type, osmium::Location> index_pos_type;
typedef osmium::handler::NodeLocationsForWays<index_pos_type> location_handler_type;


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


struct TileHandler : public osmium::handler::Handler {

    size_t m_count_way;
    size_t m_count;
    std::vector<osmium::Box> m_Tiles;
    std::vector<std::string> m_Fnms;
    std::vector<osmium::io::Writer*> m_Writers;
    std::vector<osmium::io::OutputIterator<osmium::io::Writer> > m_out_interators;

    TileHandler(const std::vector<osmium::Box>& Tiles, const std::vector<std::string>& Files) :
        m_Tiles(Tiles),
        m_Fnms(Files) 
    { 
        m_count = 0;
        m_count_way = 0;

        std::vector<osmium::Box>::iterator iter_bbox;
        std::vector<std::string>::iterator iter_fnm;

        for (iter_bbox = m_Tiles.begin(), iter_fnm = m_Fnms.begin(); iter_bbox != m_Tiles.end(); ++iter_bbox, ++iter_fnm)
        {
            osmium::io::Header header;
            header.set("generator", "osmium_tile");
            header.add_box(*iter_bbox);

            osmium::io::Writer *writer = new osmium::io::Writer(*iter_fnm, header);
            m_Writers.push_back(writer);
						m_out_interators.push_back(osmium::io::make_output_iterator(*writer));
        }
        
    }
    
    ~TileHandler()
    {
        std::cout << "\nwritten " << m_count << " nodes, " << m_count_way << " ways\n";

				for (size_t i=0; i < m_Writers.size(); ++i)
				{
          osmium::io::Writer *writer = m_Writers[i];
          writer->close();
					delete writer;
        }
    }

    void node(const osmium::Node& node) {
        
        std::vector<osmium::Box>::iterator iter_bbox;
        std::vector<osmium::io::OutputIterator<osmium::io::Writer> >::iterator iter_w;

        for (iter_bbox = m_Tiles.begin(), iter_w = m_out_interators.begin(); iter_bbox != m_Tiles.end(); ++iter_bbox, ++iter_w)
        {
          if ((node.location().lon() > iter_bbox->bottom_left().lon()) && (node.location().lon() < iter_bbox->top_right().lon()))
            if ((node.location().lat() > iter_bbox->bottom_left().lat()) && (node.location().lat() < iter_bbox->top_right().lat()))
              *iter_w++ = node;
        }

        m_count++;
        if ((m_count % 10000) == 0)
        {
           fprintf(stderr, "         \r %ld nodes...", m_count);
        }
    }
    
    void way(const osmium::Way& way) {
        osmium::Box wbounds;

        for (osmium::WayNodeList::const_iterator iter = way.nodes().begin(); iter != way.nodes().end(); ++iter)
        {
           wbounds.extend(iter->location());
        }
        
        std::vector<osmium::Box>::iterator iter_bbox;
        std::vector<osmium::io::OutputIterator<osmium::io::Writer> >::iterator iter_w;

        for (iter_bbox = m_Tiles.begin(), iter_w = m_out_interators.begin(); iter_bbox != m_Tiles.end(); ++iter_bbox, ++iter_w)
        {
           if ((wbounds.top_right().lon() > iter_bbox->bottom_left().lon()) && (wbounds.bottom_left().lon() < iter_bbox->top_right().lon()))
             if ((wbounds.top_right().lat() > iter_bbox->bottom_left().lat()) && (wbounds.bottom_left().lat() < iter_bbox->top_right().lat()))
						 {
							 *iter_w++ = way;
						 }
				}
				

        m_count_way++;
        if ((m_count_way % 1000) == 0)
        {
            fprintf(stderr, "         \r %ld ways...", m_count_way);
        }
    }

};

void print_help() {
    std::cout << "osmium_tile [OPTIONS] OSMFILE\n"
              << "\nOptions:\n"
              << "  -h, --help           - This help message\n"
              << "  -o, --output=OSMFILES - Where to write output (file list separated by ':')\n"
              << "  -b, --bounds=BOUNDS - Bounds of output files (list separated by ':', coordinates by ',')\n"
              << "  -v, --verbose        - Verbose output\n"
              << "  -V, --version        - Show version and exit\n"
              << "\n";
}

int main(int argc, char* argv[]) {
    const auto& map_factory = osmium::index::MapFactory<osmium::unsigned_object_id_type, osmium::Location>::instance();

    std::string output_filenames;
    std::string output_bounds;
    bool verbose = false;

    static struct option long_options[] = {
        {"help",         no_argument, 0, 'h'},
        {"output", required_argument, 0, 'o'},
        {"bounds", required_argument, 0, 'b'},
        {"verbose",      no_argument, 0, 'v'},
        {"version",      no_argument, 0, 'V'},
        {0, 0, 0, 0}
    };

    std::string location_store { "sparse_mem_array" };
    
    while (1) {
        int c = getopt_long(argc, argv, "ho:b:vV", long_options, 0);
        if (c == -1)
            break;

        switch (c) {
            case 'h':
                print_help();
                exit(0);
            case 'o':
                output_filenames = optarg;
                break;
            case 'b':
                output_bounds = optarg;
                break;
            case 'v':
                verbose = true;
                break;
            case 'V':
                std::cout << "osmium_tile\n"
                          << "Copyright (C) 2016 Christoph Hormann <chris_hormann@gmx.de>\n"
                          << "Copyright (C) 2012-2016  Jochen Topf <jochen@topf.org>\n"
                          << "License: GNU GENERAL PUBLIC LICENSE Version 3 <http://gnu.org/licenses/gpl.html>.\n"
                          << "This is free software: you are free to change and redistribute it.\n"
                          << "There is NO WARRANTY, to the extent permitted by law.\n";
                exit(0);
            default:
                exit(1);
        }
    }

    // The vout object is an output stream we can write to instead of
    // std::cerr. Nothing is written if we are not in verbose mode.
    // The running time will be prepended to output lines.
    osmium::util::VerboseOutput vout(verbose);

    if (output_filenames.empty()) {
        std::cerr << "Missing -o/--output=OSMFILE option\n";
        exit(1);
    }

    if (output_bounds.empty()) {
        std::cerr << "Missing -b/--bounds=BOUNDS option\n";
        exit(1);
    }

    if (optind != argc - 1) {
        std::cerr << "Usage: osmium_tile [OPTIONS] OSMFILE\n";
        exit(1);
    }

    const std::vector<std::string> FnmsOut = split(output_filenames, ':');
    const std::vector<std::string> SBounds = split(output_bounds, ':');
    std::vector<osmium::Box> Bounds;

    if (FnmsOut.size() != SBounds.size()) {
        std::cerr << "Bounds and filename list have different length\n";
        exit(1);
    }

    std::cerr << "Tiling into " << FnmsOut.size() << " tiles...\n";

    for (size_t i = 0; i < FnmsOut.size(); i++)
    {
        std::vector<std::string> bcoords = split(SBounds[i], ',');
        osmium::Box b;
        osmium::Location p1(atof(bcoords[0].c_str()), atof(bcoords[1].c_str()));
        b.extend(p1);
        osmium::Location p2(atof(bcoords[2].c_str()), atof(bcoords[3].c_str()));
        b.extend(p2);

        Bounds.push_back(b);
    }

    try {
      
        osmium::io::Reader reader(argv[optind], osmium::osm_entity_bits::node | osmium::osm_entity_bits::way);

        std::unique_ptr<index_pos_type> index_pos = map_factory.create_map(location_store);
        location_handler_type location_handler(*index_pos);
        location_handler.ignore_errors();

        TileHandler tile_handler(Bounds, FnmsOut);

        osmium::apply(reader, location_handler, tile_handler);   
        reader.close();

    } catch (osmium::io_error& e) {
        std::cerr << "io error: " << e.what() << "'\n";
        exit(1);
    }
    
}

