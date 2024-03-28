#include <iostream>
#include <fstream>
#include <vector>
using namespace std;

#include "../cauchy_256.h"
#include "../SiameseTools.h"
#include <cstdint>

#ifdef _WIN32
#include "getopt.h"
#else
#include <unistd.h>
#endif

static int original_count_k_ = 48;
static int recovery_count_m_ = 96;
static int block_bytes_l_ = 1400;
static int trials_n_ = 1000;

//------------------------------------------------------------------------------
// Utility: Deck Shuffling function

void ShuffleDeck16(
    siamese::PCGRandom& prng,
    uint16_t* deck,
    const uint32_t count)
{
    deck[0] = 0;

    // If we can unroll 4 times:
    if (count <= 256)
    {
        for (uint32_t ii = 1;;)
        {
            uint32_t jj, rv = prng.Next();

            // 8-bit unroll
            switch (count - ii)
            {
            default:
                jj = (uint8_t)rv % ii;
                deck[ii] = deck[jj];
                deck[jj] = (uint16_t)ii;
                ++ii;
                jj = (uint8_t)(rv >> 8) % ii;
                deck[ii] = deck[jj];
                deck[jj] = (uint16_t)ii;
                ++ii;
                jj = (uint8_t)(rv >> 16) % ii;
                deck[ii] = deck[jj];
                deck[jj] = (uint16_t)ii;
                ++ii;
                jj = (uint8_t)(rv >> 24) % ii;
                deck[ii] = deck[jj];
                deck[jj] = (uint16_t)ii;
                ++ii;
                break;

            case 3:
                jj = (uint8_t)rv % ii;
                deck[ii] = deck[jj];
                deck[jj] = (uint16_t)ii;
                ++ii;
            case 2:
                jj = (uint8_t)(rv >> 8) % ii;
                deck[ii] = deck[jj];
                deck[jj] = (uint16_t)ii;
                ++ii;
            case 1:
                jj = (uint8_t)(rv >> 16) % ii;
                deck[ii] = deck[jj];
                deck[jj] = (uint16_t)ii;
            case 0:
                return;
            }
        }
    }
    else
    {
        // For each deck entry:
        for (uint32_t ii = 1;;)
        {
            uint32_t jj, rv = prng.Next();

            // 16-bit unroll
            switch (count - ii)
            {
            default:
                jj = (uint16_t)rv % ii;
                deck[ii] = deck[jj];
                deck[jj] = (uint16_t)ii;
                ++ii;
                jj = (uint16_t)(rv >> 16) % ii;
                deck[ii] = deck[jj];
                deck[jj] = (uint16_t)ii;
                ++ii;
                break;

            case 1:
                jj = (uint16_t)rv % ii;
                deck[ii] = deck[jj];
                deck[jj] = (uint16_t)ii;
            case 0:
                return;
            }
        }
    }
}

static void print(const void *data, int bytes) {
	const uint8_t *in = reinterpret_cast<const uint8_t *>( data );

	cout << hex;
	for (int ii = 0; ii < bytes; ++ii) {
		cout << (int)in[ii] << " ";
	}
	cout << dec << endl;
}


//#define CAT_WORST_CASE_BENCHMARK
//#define CAT_REASONABLE_RECOVERY_COUNT

// Test to make sure that Longhair works well with input ordered like this for
// k = 4 and m = 2
// 0
// 2
// 3
// 5
// 6
int PerfTimingTest()
{
    const unsigned block_bytes = block_bytes_l_; // a multiple of 8

    siamese::PCGRandom prng;
	prng.Seed(siamese::GetTimeUsec());

    const unsigned block_count = original_count_k_;
    const unsigned recovery_block_count = recovery_count_m_;

    std::vector<uint8_t> data(block_bytes * block_count);
    std::vector<uint8_t> recovery_blocks(block_bytes * recovery_block_count);
    std::vector<Block> blocks(block_count);

    uint64_t sum_encode = 0;
    uint64_t sum_decode = 0;
    const int trials = trials_n_;
    cout << "Params: block-size = " << block_bytes << " k = " << block_count
         << " m = " << recovery_block_count << " trials = " << trials << endl;
    cout << "PerfTimingTest Start!\n"
         << endl;
    int last_print = -1;
    for (int trial = 0; trial < trials; ++trial)
    {
        int percent = trial * 100 / trials;
        if (0 == percent % 10 && percent != last_print)
        {
            last_print = percent;
            cout << "......%" << percent
                 << endl;
        }
        const uint8_t *data_ptrs[256];
        for (unsigned ii = 0; ii < block_count; ++ii)
        {
            data_ptrs[ii] = &data[ii * block_bytes];
        }

        for (unsigned ii = 0; ii < block_bytes * block_count; ++ii)
        {
            data[ii] = (uint8_t)prng.Next();
        }

        const uint64_t t0 = siamese::GetTimeUsec();
        const int encodeResult = cauchy_256_encode(
            block_count,
            recovery_block_count,
            data_ptrs,
            &recovery_blocks[0],
            block_bytes);
        if (encodeResult != 0)
        {
            cout << "Encode failed" << endl;
            SIAMESE_DEBUG_BREAK();
            return 1;
        }

        const uint64_t t1 = siamese::GetTimeUsec();
        const uint64_t encode_time = t1 - t0;
        sum_encode += encode_time;

        for (unsigned ii = 0; ii < block_count; ++ii)
        {
            blocks[ii].data = (uint8_t *)data_ptrs[ii];
            blocks[ii].row = (uint8_t)ii;
        }

        int rem = block_count;
        for (unsigned ii = 0; ii < recovery_block_count && ii < block_count; ++ii)
        {
            unsigned jj = prng.Next() % rem;

            --rem;

            for (unsigned kk = jj; kk < rem; ++kk)
            {
                blocks[kk].data = blocks[kk + 1].data;
                blocks[kk].row = blocks[kk + 1].row;
            }

            blocks[rem].data = &recovery_blocks[ii * block_bytes];
            blocks[rem].row = block_count + ii;
        }

        // cout << "Before decode:" << endl;
        // for (unsigned ii = 0; ii < block_count; ++ii)
        // {
        //     cout << (int)blocks[ii].row << endl;
        // }

        const uint64_t t2 = siamese::GetTimeUsec();
        const int decodeResult = cauchy_256_decode(
            block_count,
            recovery_block_count,
            &blocks[0],
            block_bytes);
        if (decodeResult != 0)
        {
            cout << "Decode failed" << endl;
            SIAMESE_DEBUG_BREAK();
            return 1;
        }

        const uint64_t t3 = siamese::GetTimeUsec();
        const uint64_t decode_time = t3 - t2;
        sum_decode += decode_time;

        // cout << "After decode:" << endl;
        // for (unsigned ii = 0; ii < block_count; ++ii)
        // {
        //     cout << (int)blocks[ii].row << endl;
        // }

        int result = 0;
        for (int ii = 0; ii < block_count; ++ii)
        {
            result |= memcmp(blocks[ii].data, data_ptrs[blocks[ii].row], block_bytes);
        }
        SIAMESE_DEBUG_ASSERT(result == 0);
    }

    const double opusec_enc = sum_encode / static_cast<double>(trials);
    const double mbps_enc = (block_bytes * block_count / opusec_enc);
    const double opusec_dec = sum_decode / static_cast<double>(trials);
    const double mbps_dec = (block_bytes * block_count / opusec_dec);

    cout << "\nPerfTimingTest End!\n"
         << endl;
    cout << "Encoder: " << opusec_enc << " usec, " << mbps_enc << " MBps" << endl;
    cout << "Decoder: " << opusec_dec << " usec, " << mbps_dec << " MBps" << endl;

    return 0;
}

static inline void show_help()
{
    fprintf(stdout, "Usage: ./longhair_test [options] trials_cnt \n");
    fprintf(stdout, "Eg: ./longhair_test -k 48 -m 96 -l 1400 1000 \n");
    fprintf(stdout, "Print help:\n"
                    "-h         show help\n"
                    "\n");
    fprintf(stdout, "Input specification:\n"
                    "-k cnt     set original count\n"
                    "-m cnt     set recovery count\n"
                    "-l bytes   set block bytes\n"
                    "\n");
}

int main(int argc, char **argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "hk:m:l:")) != EOF)
    {
        switch (opt)
        {
        case 'h':
            show_help();
            exit(EXIT_FAILURE);
        case 'k':
            original_count_k_ = atoi(optarg);
            break;
        case 'm':
            recovery_count_m_ = atoi(optarg);
            break;
        case 'l':
            block_bytes_l_ = atoi(optarg);
            break;
        default: /* '?' */
            fprintf(stdout, "Use \'-h\' to get more help\n");
            exit(EXIT_FAILURE);
        }
    }

    if (optind >= argc)
    {
        fprintf(stdout, "Trials count must be specified %d-%d\n", optind, argc);
        exit(EXIT_FAILURE);
    }

    trials_n_ = atoi(argv[optind]);

    cauchy_256_init();

    cout << "Cauchy RS Codec Unit Tester" << endl;

    if (0 != PerfTimingTest())
    {
        cout << "PerfTimingTest failed" << endl;
        return 1;
    }

#if 1
    return 0;
#endif

    const unsigned block_bytes = 8 * 162; // a multiple of 8

	cout << "Using " << block_bytes << " bytes per block (ie. packet/chunk size); must be a multiple of 8 bytes" << endl;

    siamese::PCGRandom prng;
    prng.Seed(siamese::GetTimeUsec());

	uint8_t heat_map[256 * 256] = { 0 };

	for (unsigned block_count = 1; block_count < 256; ++block_count) {
#ifdef CAT_REASONABLE_RECOVERY_COUNT
		for (unsigned recovery_block_count = 1; recovery_block_count <= (block_count / 2) && recovery_block_count < (256 - block_count); ++recovery_block_count) {
#else
		for (unsigned recovery_block_count = 1; recovery_block_count < (256 - block_count); ++recovery_block_count) {
#endif
            std::vector<uint8_t> data(block_bytes * block_count);
            std::vector<uint8_t> recovery_blocks(block_bytes * recovery_block_count);
            std::vector<Block> blocks(block_count);

#if 0
            if (recovery_block_count != 1) {
                continue;
            }
#endif

            const uint8_t *data_ptrs[256];
			for (int ii = 0; ii < block_count; ++ii) {
				data_ptrs[ii] = &data[ii * block_bytes];
			}

            uint64_t sum_encode = 0;

			unsigned erasures_count;
#ifdef CAT_WORST_CASE_BENCHMARK
            erasures_count = recovery_block_count;
            if (block_count < erasures_count)
            {
                erasures_count = block_count;
            }
            {
#else
			for (erasures_count = 1; erasures_count <= recovery_block_count && erasures_count <= block_count; ++erasures_count) {
#endif
				for (unsigned ii = 0; ii < block_bytes * block_count; ++ii) {
					data[ii] = (uint8_t)prng.Next();
				}

                const uint64_t t0 = siamese::GetTimeUsec();

                const int encodeResult = cauchy_256_encode(
                    block_count,
                    recovery_block_count,
                    data_ptrs,
                    &recovery_blocks[0],
                    block_bytes);
                if (encodeResult != 0)
                {
                    cout << "Encode failed" << endl;
                    SIAMESE_DEBUG_BREAK();
                    return 1;
                }

                const uint64_t t1 = siamese::GetTimeUsec();
				const uint64_t encode_time = t1 - t0;
				sum_encode += encode_time;

                if (encode_time == 0) {
                    cout << "Encoded k=" << block_count << " data blocks with m=" << recovery_block_count
                        << " recovery blocks in " << encode_time << " usec" << endl;
                }
                else {
                    cout << "Encoded k=" << block_count << " data blocks with m=" << recovery_block_count
                        << " recovery blocks in " << encode_time << " usec : "
                        << (block_bytes * block_count / encode_time) << " MB/s" << endl;
                }

                // Select the packets to drop randomly
                uint16_t deck[256];
                ShuffleDeck16(prng, deck, block_count);

				for (unsigned ii = 0; ii < erasures_count; ++ii) {
                    unsigned erasure_index = deck[ii];
					blocks[ii].data = &recovery_blocks[ii * block_bytes];
					blocks[ii].row = block_count + ii;
				}

				for (unsigned ii = erasures_count; ii < block_count; ++ii) {
					blocks[ii].data = &data[ii * block_bytes];
					blocks[ii].row = ii;
				}

                const uint64_t t2 = siamese::GetTimeUsec();

                const int decodeResult = cauchy_256_decode(
                    block_count,
                    recovery_block_count,
                    &blocks[0],
                    block_bytes);
                if (decodeResult != 0)
                {
                    cout << "Decode failed" << endl;
                    SIAMESE_DEBUG_BREAK();
                    return 1;
                }

                const uint64_t t3 = siamese::GetTimeUsec();
                const uint64_t decode_time = t3 - t2;

                if (decode_time == 0) {
                    cout << "+ Decoded " << erasures_count << " erasures e.g. " << deck[0] << " so fast we cannot measure it" << endl;
                }
                else {
                    cout << "+ Decoded " << erasures_count << " erasures e.g. " << deck[0] << " in " << decode_time << " usec : "
                        << (block_bytes * block_count / decode_time) << " MB/s" << endl;
                }

                int result = 0;
				for (int ii = 0; ii < erasures_count; ++ii) {
                    const uint8_t *orig = &data[ii * block_bytes];
                    result |= memcmp(blocks[ii].data, orig, block_bytes);
				}
                if (result != 0)
                {
                    cout << "Data corruption" << endl;
                    SIAMESE_DEBUG_BREAK();
                    return 1;
                }
			}

            if (erasures_count == 0) {
                continue;
            }
            uint64_t avg_encode = sum_encode / erasures_count;
            unsigned speed;
            if (avg_encode == 0) {
                speed = 10000;
            }
            else {
                speed = (unsigned)((uint64_t)block_bytes * block_count / avg_encode);
            }

			uint8_t map_value = 0;

			if (speed < 10) {
				map_value = 1;
			} else if (speed < 50) {
				map_value = 2;
			} else if (speed < 100) {
				map_value = 3;
			} else if (speed < 200) {
				map_value = 4;
			} else if (speed < 300) {
				map_value = 5;
			} else if (speed < 400) {
				map_value = 6;
			} else if (speed < 500) {
				map_value = 7;
			} else {
				map_value = 8;
			}

			heat_map[block_count * 256 + recovery_block_count] = map_value;
		}
	}

	ofstream file;
	file.open("../docs/heatmap.txt");

	for (int ii = 0; ii < 256; ++ii) {
		for (int jj = 0; jj < 256; ++jj) {
			uint8_t map_value = heat_map[ii * 256 + jj];

			file << (int)map_value << " ";
		}
		file << endl;
	}

	return 0;
}
