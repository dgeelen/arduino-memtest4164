#include <algorithm>
#include <numeric>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <boost/scope_exit.hpp>

#include "font.hpp"
#include "util.hpp"


struct DBTable {
	std::size_t storage_size() const {
		return data.size() + data.size()%2; // +padding
	}
	std::vector<std::string> data;
};

std::ostream& operator<<(std::ostream& stream, const DBTable& dbt) {
	std::size_t line_len = 0;
	for(std::size_t i = 0; i < dbt.data.size(); ++i) {
		if(i % 8 == 0 || line_len > 79) {
			if(i) {
				stream << " ; " << to_hex<std::uint16_t>(i) << "\n";
			}
			stream << ".db ";
		line_len = 4;
		}
		stream << dbt.data[i] << (i+1 == dbt.data.size() ? "" : ", ");
		line_len += dbt.data[i].size() + (i+1 == dbt.data.size() ? 0 : 2);
	}
	if(dbt.data.size() % 8 != 0 && dbt.data.size() % 2 != 0) {
		stream << ", 0x00";
	}
	stream << "\n";

	return stream;
}


int main(const int argc, const char* argv[]) try {
	const char* input_fn { nullptr };
	for(int i = 1; i < argc; ++i) {
		const std::string& arg { argv[i] };
		if(arg == "-i") {
			if(i+1 >= argc) {
				throw std::runtime_error("missing argument to "+arg);
			}
		}
		else {
			// non option
			if(!input_fn) input_fn = argv[i];
		}
	}

	FILE* input { nullptr };
	if(input_fn == nullptr) {
		std::cerr << "Reading from stdin\n";
		input = stdin;
	}
	else {
		std::cerr << "Reading from '" << input_fn << "'\n";
		input = fopen(input_fn, "rb");
		if(!input) {
			throw std::runtime_error("error opening file.");
		}
	}
	BOOST_SCOPE_EXIT(&input_fn, &input) {
		if(input_fn != nullptr) {
			fclose(input);
		}
	} BOOST_SCOPE_EXIT_END

	std::cout << "#include \"abi.csm\"\n"
	          << "#include \"utility_macros.csm\"\n"
	          << "\n\n"
	          ;


	Font fnt;
	fnt.load(input);

	std::vector<std::pair<std::string, std::size_t>> bytes_used;
	std::map<std::size_t, std::set<Glyph>> size_map;
	for(const auto& glyph : fnt.glyphs()) {
		size_map[glyph.box().width()].insert(glyph);
	}
	struct GlyphTable {
		GlyphTable(
			const std::string& name,
			std::size_t glyph_width
		)
		: glyph_width{glyph_width}
		, name       {name       }
		{}
		std::size_t glyph_width;
		std::string name;
		std::vector<Glyph> glyphs;
		DBTable glyph_data;
		bool operator==(const GlyphTable& that) const {
			return this->glyph_width == that.glyph_width &&
			       this->name        == that.name &&
			       this->glyphs      == that.glyphs;
		}
	};
	std::vector<std::unique_ptr<GlyphTable>> glyph_tables;
	std::map<Glyph, const GlyphTable*> glyph_table_map;
	for(const auto& i : size_map) {
		if(std::all_of(i.second.begin(), i.second.end(), [](const auto& s) { return s.empty(); })) {
			continue;
		}
		std::size_t width = i.second.begin()->box().width();
		std::stringstream comment;
		comment << "Glyphs " << width << "px wide"
		        << " (" << i.second.size() << "x)";
		std::stringstream label;
		label << "__ssd1306_font_glyphs_" << width << "px";

		glyph_tables.emplace_back(std::make_unique<GlyphTable>(label.str(), width));
		DBTable& dbt = glyph_tables.back()->glyph_data;
		if(glyph_tables.size() == 1) {
			// Special case: the first entry of table 0 will always map to space,
			// with space being as large as the smallest character.
			for(std::size_t i = 0; i < width; ++i) {
				dbt.data.push_back("0x00"); // no pixel data
			}
			auto space = std::find_if(fnt.glyphs().begin(), fnt.glyphs().end(), [](const auto& g) {
				return g.character() == ' ';
			});
			if(space == fnt.glyphs().end()) {
				throw std::runtime_error("Space character has no glyph?!");
			}
			glyph_table_map[*space] = glyph_tables.back().get();
			glyph_tables.back()->glyphs.push_back(*space);
		}
		std::vector<std::uint8_t> table;
		for(auto s = i.second.begin(); s != i.second.end(); ++s) {
			if(s->empty()) continue;
			for(std::size_t col = s->box().left(); col < s->box().right() + 1; ++col) {
				const std::uint8_t c = s->column(col);
				assert(!(col == s->box().left() || col == s->box().right()) || c != 0);
				dbt.data.push_back(to_hex(c));
				if(s->character() == 'A') {
					std::cerr << "A:" << col << " = " << to_hex(c) << "\n";
				}
				//std::cout << '\t' << s.printable_character() << ": " << s.box() << "\n";
				//break;
			}
			glyph_table_map[*s] = glyph_tables.back().get();
			glyph_tables.back()->glyphs.push_back(*s);
		}
	}

	// This will hold all the tables, in the end
	DBTable font_data;

	/***************************************************************************
	 ** Make sure that we can later map glyph-widths to the a table           **
	 ***************************************************************************/
	// We need a table to map table entries to table pointers. By making sure
	// that the index into a table is the same as the width of the character,
	// we save having to have another table for storing widths, and make the
	// avr code a (little) bit easier. To do this we ensure that all widths
	// 0..max_width have a table.
	const std::size_t widest_glyph = (*std::max_element(
		glyph_tables.begin(),
		glyph_tables.end(),
		[](const auto& lhs, const auto& rhs) {
			return lhs->glyph_width < rhs->glyph_width;
		}
	))->glyph_width;
	for(std::size_t i = 0; i < widest_glyph + 1; ++i) {
		const auto table = std::find_if(
			glyph_tables.begin(),
			glyph_tables.end(),
			[=](const auto& e) {
				return e->glyph_width == i;
			}
		);
		if(table == glyph_tables.end()) {
			glyph_tables.emplace_back(std::make_unique<GlyphTable>("__ssd1306_font_glyphs_" + std::to_string(i) + "px", 0));
		}
	}

	// Make sure tables are sorted according to glyph width
	std::sort(glyph_tables.begin(), glyph_tables.end(), [](const auto& lhs, const auto& rhs){
		return lhs->glyph_width < rhs->glyph_width;
	});


	/***************************************************************************
	 ** ??                                                            **
	 ***************************************************************************/
	// fill out the glyph table with missing characters, mapping them to space
	// (or to '.' or '?')
	std::uint8_t first_glyph = std::min_element(
		glyph_table_map.begin(),
		glyph_table_map.end(),
		[](const auto& lhs, const auto& rhs) {
			return lhs.first.character() < rhs.first.character();
		}
	)->first.character();
	std::uint8_t last_glyph = std::max_element(
		glyph_table_map.begin(),
		glyph_table_map.end(),
		[](const auto& lhs, const auto& rhs) {
			return lhs.first.character() < rhs.first.character();
		}
	)->first.character();
	auto space = std::find_if(fnt.glyphs().begin(), fnt.glyphs().end(), [](const auto& g) {
		return g.character() == ' ';
	});
	if(space == fnt.glyphs().end()) {
		throw std::runtime_error("Space character has no glyph?!");
	}
	auto space_table = std::find_if(glyph_tables.begin(), glyph_tables.end(), [&](const auto& table) {
		return std::find(table->glyphs.begin(), table->glyphs.end(), *space) != table->glyphs.end();
	});
	assert(space_table != glyph_tables.end());
	for(char i = first_glyph; i < last_glyph; ++i) {
		if(std::find_if(glyph_table_map.begin(), glyph_table_map.end(), [=](const auto& g) {
			return g.first.character() == i;
		}) == glyph_table_map.end()) {
			glyph_table_map[*space] = space_table->get();
		}
	}

	/***************************************************************************
	 ** Determine number of bits needed to represent tables and indices       **
	 ***************************************************************************/
	const std::size_t n_table_bits = std::ceil(std::log2(glyph_tables.size()));
	const std::size_t n_index_bits = std::ceil(std::log2(
		(*std::max_element(glyph_tables.begin(), glyph_tables.end(), [](const auto& lhs, const auto& rhs) {
			return lhs->glyphs.size() < rhs->glyphs.size();
		}))->glyphs.size()
	));
	if(n_table_bits > 8 || n_index_bits > 8) {
		throw std::runtime_error("this many bits should not be needed for indexing!");
	}

	std::cout << "\n\n"
	          << "#define __ssd1306_font_first_glyph (" << to_hex(first_glyph)                       << ")\n"
	          << "#define __ssd1306_font_last_glyph ("  << to_hex(last_glyph)                        << ")\n"
	          << "#define __ssd1306_font_table_bits ("  << to_hex<std::uint8_t>(n_table_bits)        << ")\n"
	          << "#define __ssd1306_font_table_mask ("  << to_hex<std::uint8_t>((1<<n_table_bits)-1) << ")\n"
	          << "#define __ssd1306_font_index_bits ("  << to_hex<std::uint8_t>(n_index_bits)        << ")\n"
	          << "#define __ssd1306_font_index_mask ("  << to_hex<std::uint8_t>((1<<n_index_bits)-1) << ")\n"
	          << "\n"
	          ;

	/***************************************************************************
	 ** Map glyph-widths to the corresponding table                           **
	 ***************************************************************************/
	DBTable glyph_width_table;
	assert(font_data.data.size() == 0);
	std::size_t table_start_offset = glyph_tables.size()*2 +
	                                 std::ceil(((last_glyph-first_glyph+1)*(n_table_bits+n_index_bits))/8);
	for(const auto& table : glyph_tables) {
		if(table->glyph_width != 0) {
			glyph_width_table.data.push_back("low(FLASH_ADDR(__ssd1306_font_data) + " + to_hex<std::uint16_t>(table_start_offset) + ")");
			glyph_width_table.data.push_back("high(FLASH_ADDR(__ssd1306_font_data) + " + to_hex<std::uint16_t>(table_start_offset) + ")");
			std::cout << "; offset glyph width table " << table->glyph_width << "px: " << to_hex(table_start_offset) << "\n";
			table_start_offset += table->glyph_data.data.size();
		}
		else {
			glyph_width_table.data.push_back("0x00");
			glyph_width_table.data.push_back("0x00");
		}
	}
	font_data.data.insert(font_data.data.end(), glyph_width_table.data.begin(), glyph_width_table.data.end());
	bytes_used.emplace_back("Glyph-width to table mapping", glyph_width_table.data.size());

	/***************************************************************************
	 ** Map ascii characters to glyph (table and index-in-table)              **
	 ***************************************************************************/
	std::map<char, Glyph> ascii_order_glyph_map;
	for(std::uint8_t ascii = first_glyph; ascii < last_glyph; ++ascii) {
		auto glyph = std::find_if(fnt.glyphs().begin(), fnt.glyphs().end(), [=](const auto& glyph) {
			return glyph.character() == ascii;
		});
		assert(glyph != fnt.glyphs().end());
		assert(glyph_table_map.count(*glyph));
		ascii_order_glyph_map[ascii] = *glyph;
	}

	std::uint32_t packed{};
	std::size_t bits_in_pack{};
	DBTable index_table;
	for(const auto e : ascii_order_glyph_map) {
		const GlyphTable* table = glyph_table_map[e.second];
		const std::size_t nth_table = std::distance(
			glyph_tables.begin(),
			std::find_if(glyph_tables.begin(), glyph_tables.end(), [=](const auto& t) {
				return table == t.get();
			})
		);
		const std::size_t index = std::distance(
			table->glyphs.begin(),
			std::find(
				table->glyphs.begin(),
				table->glyphs.end(),
				e.second)
			);
		packed = (packed << (n_index_bits+n_table_bits)) | (index << n_table_bits) | (nth_table << 0);
		bits_in_pack += n_index_bits+n_table_bits;
		if(e.first == 'A') {
			std::cerr << "index table: A = " << to_hex<std::uint8_t>(nth_table) << "." << to_hex<std::uint8_t>(index) << "\n";
		}
		while(bits_in_pack >= 8) {
			index_table.data.push_back(to_hex<std::uint8_t>(packed>>(bits_in_pack-8)));
			bits_in_pack -= 8;
		}
	}
	assert(bits_in_pack < 8);
	while(bits_in_pack) {
		if(bits_in_pack < 8) {
			packed <<=1;
			++bits_in_pack;
		}
		else {
			index_table.data.push_back(to_hex<std::uint8_t>(packed>>(bits_in_pack-8)));
			bits_in_pack -= 8;
		}
	}
	assert(bits_in_pack == 0);
	font_data.data.insert(font_data.data.end(), index_table.data.begin(), index_table.data.end());
	bytes_used.emplace_back("Character to glyph table/index mapping", index_table.data.size());
	std::size_t ascii_order_glyph_map_offset = glyph_tables.size()*2;
	std::cout << "#define __ssd1306_font_ascii_order_glyph_map_offset (" << to_hex(ascii_order_glyph_map_offset) << ")\n";


	/***************************************************************************
	 ** Glyph data                                                            **
	 ***************************************************************************/
	bytes_used.emplace_back("Glyph data", 0);
	for(const auto& table : glyph_tables) {
		font_data.data.insert(font_data.data.end(), table->glyph_data.data.begin(), table->glyph_data.data.end());
		bytes_used.back().second += table->glyph_data.data.size();
	}
	bytes_used.back().second += font_data.data.size() % 2;

	/***************************************************************************
	 ** Output font data table                                                **
	 ***************************************************************************/
	std::cout << "__ssd1306_font_data:\n" << font_data << "\n";



	/***************************************************************************
	 ** Code                                                                  **
	 ***************************************************************************/
	std::cout << "\n\n"
	          << "; returns (in z) the address (in flash) of the first data byte" "\n"
	          << "; of the requested character (r16), and the size (width) of"    "\n"
	          << "; the character in r25."                                        "\n"
	          << "__ssd1306_font_get_data_ptr:"                                   "\n"
	          << "\tldi    zl, low(FLASH_ADDR(__ssd1306_font_data)+__ssd1306_font_ascii_order_glyph_map_offset)"  "\n"
	          << "\tldi    zh, high(FLASH_ADDR(__ssd1306_font_data)+__ssd1306_font_ascii_order_glyph_map_offset)" "\n"
	          << "\t"                                                             "\n"
	          << "\t; first of all, see if the requested character has a glyph"   "\n"
	          << "\tsubi   r16, __ssd1306_font_first_glyph"                       "\n"
	          << "\tcpi    r16, __ssd1306_font_last_glyph - __ssd1306_font_first_glyph + 1" "\n"
	          << "\tbrlo   __ssd1306_font_get_data_ptr_exists"                    "\n"
	          << "\tsubi   r16, -__ssd1306_font_first_glyph"                      "\n"
	          << "\tret"                                                          "\n"
	          << "__ssd1306_font_get_data_ptr_exists:"                            "\n"
	          << "\tsave_registers(r19, r20, r21, r22, r23, xl, xh)"              "\n"
	          <<                                                                  "\n"
	          << "\t;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;" "\n"
	          << "\t; step 1: determine the index into the character map"         "\n"
	          << "\t;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;" "\n"
	          << "\tldi    r24, __ssd1306_font_index_bits"                        "\n"
	          << "\tldi    r25, __ssd1306_font_table_bits"                        "\n"
	          << "\tin     r19, SREG"                                             "\n"
	          << "\tcli                ; because we are going to clobber r0:r1"   "\n"
	          << "\tmul    r16, r24"                                              "\n"
	          << "\tmovw   r20, rC0"                                              "\n"
	          << "\tmul    r16, r25"                                              "\n"
	          << "\tmovw   r22, rC0"                                              "\n"
	          << "\tclr    rC0"                                                   "\n"
	          << "\tclr    rC1"                                                   "\n"
	          << "\tout    SREG, r19   ; restore IE flag"                         "\n"
	          << "\tinc    rC1"                                                   "\n"
	          << "\tadd    r20, r22"                                              "\n"
	          << "\tadc    r21, r23"                                              "\n"
	          <<                                                                  "\n"
	          << "\t; r20:r21 now contains the bit index into the table,"         "\n"
	          << "\t; convert that into a byte index + bit offset"                "\n"
	          << "\tmov    xh, r20"                                               "\n"
	          << "\tandi   xh, 0x07    ; xh is the bit offset"                    "\n"
	          << "\tasr    r21"                                                   "\n"
	          << "\tror    r20"                                                   "\n"
	          << "\tasr    r21"                                                   "\n"
	          << "\tror    r20"                                                   "\n"
	          << "\tasr    r21"                                                   "\n"
	          << "\tror    r20"                                                   "\n"
	          << "\tmov    xl, r20     ; xl is the byte index"                    "\n"
	          << "\tadd    zl, xl"                                                "\n"
	          << "\tadc    zh, rC0"                                               "\n"
	          << "\t; z now contains the index to the first byte"                 "\n"
	          <<                                                                  "\n"
	          << "\t;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;" "\n"
	          << "\t; step 2: determine the table and index into the table"       "\n"
	          << "\t;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;" "\n"
	          << "\tlpm    r20, z+"                                               "\n"
	          ;
	if(n_index_bits + n_table_bits != 8) std::cout
	          << "\tlpm    r21, z+"                                               "\n";
	if(n_index_bits + n_table_bits > 9) std::cout
	          << "\tlpm    r22, z+"                                               "\n";
	if(n_index_bits + n_table_bits != 8) std::cout
	          <<                                                                  "\n"
	          << "\tmov    r24, xh    ; bit offset"                               "\n"
	          << "\tsubi   r24, -(__ssd1306_font_table_bits+__ssd1306_font_index_bits)"                                                             "\n"
	          << "\tldi    r23, 8"                                                "\n"
	          << "\tsub    r23, r24"                                              "\n"
	          << "\t; r23 = amount to shift right to align table bits"            "\n"
	          << "\tandi   r23, 0x07"                                             "\n"
	          <<                                                                  "\n"
	          << "\tmov    r24, r21"                                              "\n"
	          << "\tmov    r25, r22"                                              "\n"
	          << "__ssd1306_font_extract_table_bits:"                             "\n"
	          << "\tcp     r23, rC0"                                              "\n"
	          << "\tbreq   __ssd1306_font_extract_table_bits_done"                "\n"
	          << "\tasr    r25"                                                   "\n"
	          << "\tror    r24"                                                   "\n"
	          << "\tdec    r23"                                                   "\n"
	          << "\trjmp   __ssd1306_font_extract_table_bits"                     "\n"
	          << "__ssd1306_font_extract_table_bits_done:"                        "\n"
	          << "\tmov    xl, r24     ; xl is the table index"                   "\n"
	          << "\tandi   xl, __ssd1306_font_table_mask"                         "\n"
	          <<                                                                  "\n"
	          << "\tmov    r24, xh     ; bit offset"                              "\n"
	          << "\tsubi   r24, -(__ssd1306_font_index_bits)"                     "\n"
	          << "\tldi    r23, 8"                                                "\n"
	          << "\tsub    r23, r24"                                              "\n"
	          << "\t; r23 = amount to shift right to align index bits"            "\n"
	          << "\tandi   r23, 0x07"                                             "\n"
	          <<                                                                  "\n"
	          << "\tmov    r24, r20"                                              "\n"
	          << "\tmov    r25, r21"                                              "\n"
	          << "__ssd1306_font_extract_index_bits:"                             "\n"
	          << "\tcp     r23, rC0"                                              "\n"
	          << "\tbreq   __ssd1306_font_extract_index_bits_done"                "\n"
	          << "\tasr    r25"                                                   "\n"
	          << "\tror    r24"                                                   "\n"
	          << "\tdec    r23"                                                   "\n"
	          << "\trjmp   __ssd1306_font_extract_index_bits"                     "\n"
	          << "__ssd1306_font_extract_index_bits_done:"                        "\n"
	          << "\tmov    xh, r24     ; xh is the index in the table"            "\n"
	          << "\tandi   xh, __ssd1306_font_index_mask"                         "\n"
	          <<                                                                  "\n"
	          << "\t;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;" "\n"
	          << "\t; step 3: convert table and index into pointer"               "\n"
	          << "\t;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;" "\n"
	          << "\tldi    zl, low(FLASH_ADDR(__ssd1306_font_data))"              "\n"
	          << "\tldi    zh, high(FLASH_ADDR(__ssd1306_font_data))"             "\n"
	          << "\tadd    zl, xl"                                                "\n"
	          << "\tadc    zh, rC0"                                               "\n"
	          << "\tadd    zl, xl"                                                "\n"
	          << "\tadc    zh, rC0     ; add 2*table_index"                       "\n"
	          << "\tlpm    r24, z+"                                               "\n"
	          << "\tlpm    r25, z+     ; load table pointer"                      "\n"
	          << "\tmovw   zl, r24     ; z = start of table"                      "\n"
	          <<                                                                  "\n"
	          << "\t; locate correct index (depends on the width of the glyph)"   "\n"
	          << "\tin     r19, SREG"                                             "\n"
	          << "\tcli                ; because we are going to clobber r0:r1"   "\n"
	          << "\tmul    xh, xl      ; nth-table entry to byte offset"          "\n"
	          << "\tmovw   r24, rC0    ; r24:r25 is byte offset in table"         "\n"
	          << "\tclr    rC0"                                                   "\n"
	          << "\tclr    rC1"                                                   "\n"
	          << "\tout    SREG, r19   ; restore IE flag"                         "\n"
	          << "\tinc    rC1"                                                   "\n"
	          <<                                                                  "\n"
	          << "\tadd    zl, r24"                                               "\n"
	          << "\tadc    zh, r25     ; z points to first byte of glyph"         "\n"
	          << "\tmov    r25, xl     ; r25 is the length of the glyph"          "\n"
	          ;
	std::cout
	          << "\trestore_registers(r19, r20, r21, r22, r23, xl, xh)"           "\n"
	          << "\tret"                                                          "\n"
	          ;


	const std::size_t n_glyphs = std::accumulate(fnt.glyphs().begin(), fnt.glyphs().end(), 0, [](const std::size_t a, const auto& g) { return a + !g.empty(); });
	std::cout << "\n\n"
	          << "; Number of glyphs: "   << n_glyphs << '\n';
	bytes_used.emplace_back("Total storage used", font_data.storage_size());
	for(const auto& e : bytes_used) {
		std::cout << "; " << e.first << ": " << e.second
		          << " bytes (~" << std::setprecision(3) << (e.second / double(n_glyphs)) << " bytes/glyph)\n";
	}

	return 0 ;
}
catch(const std::exception& e) {
	std::cerr << "Fatal error: " << e.what() << std::endl;
	return 1;
}
