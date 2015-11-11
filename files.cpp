/*
  Copyright (c) 2015 Genome Research Ltd.

  Author: Jouni Siren <jouni.siren@iki.fi>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include "files.h"

namespace gcsa
{

//------------------------------------------------------------------------------

bool
tokenize(const std::string& line, std::vector<std::string>& tokens)
{
  {
    std::string token;
    std::istringstream ss(line);
    while(std::getline(ss, token, '\t'))
    {
      tokens.push_back(token);
    }
    if(tokens.size() != 5)
    {
      std::cerr << "tokenize(): The kmer line must contain 5 tokens" << std::endl;
      std::cerr << "tokenize(): The line was: " << line << std::endl;
      return false;
    }
  }

  // Split the list of successor positions into separate tokens.
  std::string destinations = tokens[4], token;
  std::istringstream ss(destinations);
  tokens.resize(4);
  while(std::getline(ss, token, ','))
  {
    tokens.push_back(token);
  }

  return true;
}

size_type
readText(std::istream& in, std::vector<KMer>& kmers, bool append)
{
  if(!append) { sdsl::util::clear(kmers); }

  Alphabet alpha;
  size_type kmer_length = ~(size_type)0;
  while(true)
  {
    std::string line;
    std::getline(in, line);
    if(in.eof()) { break; }

    std::vector<std::string> tokens;
    if(!tokenize(line, tokens)) { continue; }
    if(kmer_length == ~(size_type)0)
    {
      kmer_length = tokens[0].length();
      if(kmer_length == 0 || kmer_length > Key::MAX_LENGTH)
      {
        std::cerr << "readText(): Invalid kmer length: " << kmer_length << std::endl;
        std::exit(EXIT_FAILURE);
      }
    }
    else if(tokens[0].length() != kmer_length)
    {
      std::cerr << "readText(): Invalid kmer length: " << tokens[0].length()
                << " (expected " << kmer_length << ")" << std::endl;
      std::exit(EXIT_FAILURE);
    }

    for(size_type successor = 4; successor < tokens.size(); successor++)
    {
      kmers.push_back(KMer(tokens, alpha, successor));
    }
  }

  return kmer_length;
}

//------------------------------------------------------------------------------

size_type
readBinary(std::istream& in, std::vector<KMer>& kmers, bool append)
{
  if(!append) { sdsl::util::clear(kmers); }

  size_type kmer_length = ~(size_type)0;
  size_type section = 0;
  while(true)
  {
    GraphFileHeader header(in);
    if(in.eof()) { break; }
    if(header.flags != 0)
    {
      std::cerr << "readBinary(): Invalid flags in section " << section << ": " << header.flags << std::endl;
      std::exit(EXIT_FAILURE);
    }
    if(kmer_length == ~(size_type)0)
    {
      kmer_length = header.kmer_length;
      if(kmer_length == 0 || kmer_length > Key::MAX_LENGTH)
      {
        std::cerr << "readBinary(): Invalid kmer length in section " << section << ": "
                  << kmer_length << std::endl;
        std::exit(EXIT_FAILURE);
      }
    }
    else if(header.kmer_length != kmer_length)
    {
      std::cerr << "readBinary(): Invalid kmer length in section " << section << ": "
                << header.kmer_length << " (expected " << kmer_length << ")" << std::endl;
      std::exit(EXIT_FAILURE);
    }

    size_type old_size = kmers.size();
    kmers.resize(old_size + header.kmer_count);
    in.read((char*)(kmers.data() + old_size), header.kmer_count * sizeof(KMer));
    section++;
  }

  return kmer_length;
}

//------------------------------------------------------------------------------

void
writeBinary(std::ostream& out, std::vector<KMer>& kmers, size_type kmer_length)
{
  GraphFileHeader header(kmers.size(), kmer_length);
  header.serialize(out);
  out.write((char*)(kmers.data()), header.kmer_count * sizeof(KMer));
}

void
writeKMers(const std::string& base_name, std::vector<KMer>& kmers, size_type kmer_length)
{
  std::string filename = base_name + InputGraph::BINARY_EXTENSION;
  std::ofstream output(filename.c_str(), std::ios_base::binary);
  if(!output)
  {
    std::cerr << "writeKMers(): Cannot open output file " << filename << std::endl;
    return;
  }

  writeBinary(output, kmers, kmer_length);
  output.close();

#ifdef VERBOSE_STATUS_INFO
  std::cerr << "writeKMers(): Wrote " << kmers.size() << " kmers of length " << kmer_length << std::endl;
#endif
}

//------------------------------------------------------------------------------

GraphFileHeader::GraphFileHeader() :
  flags(0), kmer_count(0), kmer_length(0)
{
}

GraphFileHeader::GraphFileHeader(size_type kmers, size_type length) :
  flags(0), kmer_count(kmers), kmer_length(length)
{
}

GraphFileHeader::GraphFileHeader(std::istream& in)
{
  in.read((char*)this, sizeof(*this));
}

GraphFileHeader::~GraphFileHeader()
{
}

size_type
GraphFileHeader::serialize(std::ostream& out)
{
  out.write((char*)this, sizeof(*this));
  return sizeof(*this);
}

//------------------------------------------------------------------------------

range_type
parseKMer(const std::string& kmer_line)
{
  std::string extensions;
  range_type result(InputGraph::UNKNOWN, 0);
  {
    std::string token;
    std::istringstream ss(kmer_line);
    if(!std::getline(ss, token, '\t')) { return result; }
    result.first = token.length();  // kmer length
    for(size_type i = 0; i < 3; i++)
    {
      if(!std::getline(ss, token, '\t')) { return result; }
    }
    if(!std::getline(ss, extensions, '\t')) { return result; }
  }

  {
    std::string token;
    std::istringstream ss(extensions);
    while(std::getline(ss, token, ',')) { result.second++; }
  }

  return result;
}

/*
  If the kmer includes one or more endmarkers, the successor position is past
  the GCSA sink node. Those kmers are marked as sorted, as they cannot be
  extended.

  FIXME Later: Is this really needed?
*/
void
markSinkNode(std::vector<KMer>& kmers)
{
  size_type sink_node = InputGraph::UNKNOWN;
  for(size_type i = 0; i < kmers.size(); i++)
  {
    if(Key::label(kmers[i].key) == 0) { sink_node = Node::id(kmers[i].from); break; }
  }
  for(size_type i = 0; i < kmers.size(); i++)
  {
    if(Node::id(kmers[i].to) == sink_node && Node::offset(kmers[i].to) > 0) { kmers[i].makeSorted(); }
  }
}

//------------------------------------------------------------------------------

const std::string InputGraph::BINARY_EXTENSION = ".graph";
const std::string InputGraph::TEXT_EXTENSION = ".gcsa2";

InputGraph::InputGraph(size_type file_count, char** base_names, bool binary_format)
{
  this->binary = binary_format;
  this->kmer_count = 0; this->kmer_length = UNKNOWN;
  for(size_type file = 0; file < file_count; file++)
  {
    std::string filename = std::string(base_names[file]) + (binary ? BINARY_EXTENSION : TEXT_EXTENSION);
    this->filenames.push_back(filename);
  }
  this->sizes = std::vector<size_type>(file_count, 0);

  // Read the files and determine kmer_count, kmer_length.
  for(size_type file = 0; file < this->files(); file++)
  {
    std::ifstream input; this->open(input, file);
    if(this->binary)
    {
      while(true)
      {
        GraphFileHeader header(input);
        if(input.eof()) { break; }
        this->kmer_count += header.kmer_count;
        this->setK(header.kmer_length, file);
        this->sizes[file] += header.kmer_count;
        input.seekg(header.kmer_count * sizeof(KMer), std::ios_base::cur);
      }
    }
    else
    {
      while(true)
      {
        std::string line;
        std::getline(input, line);
        if(input.eof()) { break; }
        range_type temp = parseKMer(line);
        this->setK(temp.first, file);
        this->kmer_count += temp.second; this->sizes[file] += temp.second;
      }

    }
    input.close();
  }

#ifdef VERBOSE_STATUS_INFO
  std::cerr << "InputGraph::InputGraph(): " << this->size() << " kmers in "
            << this->files() << " file(s)" << std::endl;
#endif
}

void
InputGraph::open(std::ifstream& input, size_type file) const
{
  if(file >= this->files())
  {
    std::cerr << "InputGraph::open(): Invalid file number: " << file << std::endl;
    std::exit(EXIT_FAILURE);
  }

  input.open(this->filenames[file].c_str(), std::ios_base::binary);
  if(!input)
  {
    std::cerr << "InputGraph::open(): Cannot open graph file " << this->filenames[file] << std::endl;
    std::exit(EXIT_FAILURE);
  }
}

void
InputGraph::setK(size_type new_k, size_type file)
{
  if(this->k() == UNKNOWN) { this->kmer_length = new_k; }
  this->checkK(new_k, file);
}

void
InputGraph::checkK(size_type new_k, size_type file) const
{
  if(new_k != this->k())
  {
    std::cerr << "InputGraph::checkK(): Invalid kmer length in graph file " << this->filenames[file] << std::endl;
    std::cerr << "InputGraph::checkK(): Expected " << this->k() << ", got " << new_k << std::endl;
    std::exit(EXIT_FAILURE);
  }
}

//------------------------------------------------------------------------------

void
InputGraph::read(std::vector<KMer>& kmers) const
{
  sdsl::util::clear(kmers);
  kmers.reserve(this->size());

  for(size_type file = 0; file < this->files(); file++)
  {
    this->read(kmers, file, true);
  }

#ifdef VERBOSE_STATUS_INFO
  std::cerr << "InputGraph::read(): Read " << kmers.size() << " kmers of length " << this->k() << std::endl;
#endif

  markSinkNode(kmers);
}

void
InputGraph::read(std::vector<KMer>& kmers, size_type file, bool append) const
{
  if(!append) { sdsl::util::clear(kmers); }

  std::ifstream input; this->open(input, file);
  if(!append) { kmers.reserve(this->sizes[file]); }
  size_type new_k = (this->binary ? readBinary(input, kmers, append) : readText(input, kmers, append));
  this->checkK(new_k, file);
  input.close();

#ifdef VERBOSE_STATUS_INFO
  if(!append)
  {
    std::cerr << "InputGraph::read(): Read " << kmers.size() << " kmers of length " << this->k()
              << " from " << this->filenames[file] << std::endl;
  }
#endif

  if(!append) { markSinkNode(kmers); }
}

void
InputGraph::read(std::vector<key_type>& keys) const
{
  sdsl::util::clear(keys);
  keys.reserve(this->size());

  // Read the keys.
  for(size_type file = 0; file < this->files(); file++)
  {
    std::vector<KMer> kmers;
    this->read(kmers, file, false);
    for(size_type i = 0; i < this->sizes[file]; i++)
    {
      keys.push_back(kmers[i].key);
    }
  }

  // Sort the keys and merge the ones sharing the same label.
  parallelQuickSort(keys.begin(), keys.end());
  size_type i = 0;
  for(size_type j = 1; j < keys.size(); j++)
  {
    if(Key::label(keys[i]) == Key::label(keys[j])) { keys[i] = Key::merge(keys[i], keys[j]); }
    else { i++; keys[i] = keys[j]; }
  }
  keys.resize(i + 1);

#ifdef VERBOSE_STATUS_INFO
  std::cerr << "InputGraph::read(): " << keys.size() << " unique keys" << std::endl;
#endif
}

//------------------------------------------------------------------------------

const std::string PathGraph::PREFIX = ".gcsa";

PathGraph::PathGraph(const InputGraph& source, sdsl::sd_vector<>& key_exists)
{
  this->path_count = 0; this->rank_count = 0;
  this->order = source.k();
  this->unique = UNKNOWN; this->unsorted = UNKNOWN; this->nondeterministic = UNKNOWN;

  sdsl::sd_vector<>::rank_1_type key_rank(&key_exists);
  for(size_type file = 0; file < source.files(); file++)
  {
    std::string temp_file = tempFile(PREFIX);
    this->filenames.push_back(temp_file);
    this->sizes.push_back(source.sizes[file]); this->path_count += source.sizes[file];
    this->rank_counts.push_back(2 * source.sizes[file]); this->rank_count += 2 * source.sizes[file];

    std::ofstream out(temp_file.c_str(), std::ios_base::binary);
    if(!out)
    {
      std::cerr << "PathGraph::PathGraph(): Cannot open output file " << temp_file << std::endl;
      std::exit(EXIT_FAILURE);
    }

    // Read KMers, sort them, and convert them to PathNodes.
    std::vector<KMer> kmers;
    source.read(kmers, file);
    parallelQuickSort(kmers.begin(), kmers.end());
    std::vector<PathNode::rank_type> temp_labels = PathNode::dummyRankVector();
    for(size_type i = 0; i < kmers.size(); i++)
    {
      kmers[i].key = Key::replace(kmers[i].key, key_rank(Key::label(kmers[i].key)));
      PathNode temp(kmers[i], temp_labels);
      temp.serialize(out, temp_labels);
      temp_labels.resize(0);
    }
    out.close();
  }

#ifdef VERBOSE_STATUS_INFO
  std::cerr << "PathGraph::PathGraph(): " << this->size() << " paths with " << this->ranks() << " ranks in "
            << this->files() << " file(s)" << std::endl;
#endif
}

PathGraph::PathGraph(size_type file_count, size_type path_order) :
  filenames(file_count), sizes(file_count, 0), rank_counts(file_count, 0),
  path_count(0), rank_count(0), order(path_order),
  unique(0), unsorted(0), nondeterministic(0)
{
  for(size_type file = 0; file < this->files(); file++)
  {
    this->filenames[file] = tempFile(PREFIX);
  }
}

PathGraph::~PathGraph()
{
  this->clear();
}

void
PathGraph::clear()
{
  for(size_type file = 0; file < this->files(); file++)
  {
    remove(this->filenames[file].c_str());
  }
  this->filenames.clear();
  this->sizes.clear();
  this->rank_counts.clear();

  this->path_count = 0; this->rank_count = 0;
  this->order = 0;
  this->unique = UNKNOWN; this->unsorted = UNKNOWN; this->nondeterministic = UNKNOWN;
}

void
PathGraph::swap(PathGraph& another)
{
  this->filenames.swap(another.filenames);
  this->sizes.swap(another.sizes);
  this->rank_counts.swap(another.rank_counts);

  std::swap(this->path_count, another.path_count);
  std::swap(this->rank_count, another.rank_count);
  std::swap(this->order, another.order);

  std::swap(this->unique, another.unique);
  std::swap(this->unsorted, another.unsorted);
  std::swap(this->nondeterministic, another.nondeterministic);
}

void
PathGraph::open(std::ifstream& input, size_type file) const
{
  if(file >= this->files())
  {
    std::cerr << "PathGraph::open(): Invalid file number: " << file << std::endl;
    std::exit(EXIT_FAILURE);
  }

  input.open(this->filenames[file].c_str(), std::ios_base::binary);
  if(!input)
  {
    std::cerr << "PathGraph::open(): Cannot open graph file " << this->filenames[file] << std::endl;
    std::exit(EXIT_FAILURE);
  }
}

//------------------------------------------------------------------------------

struct PathGraphBuilder
{
  PathGraph graph;
  std::vector<std::ofstream> files;

  PathGraphBuilder(size_type file_count, size_type path_order) :
    graph(file_count, path_order), files(file_count)
  {
    for(size_type file = 0; file < this->files.size(); file++)
    {
      this->files[file].open(this->graph.filenames[file].c_str(), std::ios_base::binary);
      if(!(this->files[file]))
      {
        std::cerr << "PathGraphBuilder::PathGraphBuilder(): Cannot open output file "
                  << this->graph.filenames[file] << std::endl;
        std::exit(EXIT_FAILURE);
      }
    }
  }

  void close()
  {
    for(size_type file = 0; file < this->files.size(); file++)
    {
      this->files[file].close();
    }
  }
/*
  void insert(... path, size_type file)
  {
    // FIXME write path to this->files[file]
    this->graph.sizes[file]++; this->graph.path_count++;
    this->graph.rank_counts[file] += ...; this->graph.rank_count += ...;
  }*/
};

// FIXME Structure for the priority queues
// PathNode node
// PathLabel first, last
// size_type file
/*
void
PathGraph::prune(const LCP& lcp)
{
#ifdef VERBOSE_STATUS_INFO
  size_type old_path_count = this->size();
#endif

  PathGraphBuilder builder(this->files(), this->k());

  // FIXME Multiway merge all files using a priority queue
  for(range_type range = firstRange(...); range.first < this->size(); range = nextRange(range, ...))
  {
    if(sameFrom(range, ...))
    {
      range_type range_lcp = extendRange(range, ..., lcp);
      mergePathNodes(range, ..., range_lcp, lcp);
      // FIXME Write to the corresponding new file
      builder.graph.unique++;
    }
    else
    {
      for(size_type i = range.first; i <= range.second; i++)
      {
        if(paths[i].sorted()) { builder.graph.nondeterministic++; }
        else { builder.graph.unsorted++; }
        // FIXME Write to the corresponding new file
      }
    }
  }
  this->clear(); builder.close();
  this->swap(builder.graph);

#ifdef VERBOSE_STATUS_INFO
  std::cerr << "  PathGraph::prune(): " << old_path_count << " -> " << paths.size() << " paths" << std::endl;
  std::cerr << "  PathGraph::prune(): " << this->unique << " unique, " << this->unsorted << " unsorted, "
            << this->nondeterministic << " nondeterministic paths" << std::endl;
#endif
}*/

//------------------------------------------------------------------------------

void
PathGraph::read(std::vector<PathNode>& paths, std::vector<PathNode::rank_type>& labels) const
{
  sdsl::util::clear(paths); sdsl::util::clear(labels);
  paths.reserve(this->size()); labels.reserve(this->ranks());

  for(size_type file = 0; file < this->files(); file++)
  {
    this->read(paths, labels, file, true);
  }

#ifdef VERBOSE_STATUS_INFO
  std::cerr << "PathGraph::read(): Read " << paths.size() << " order-" << this->k() << " paths" << std::endl;
#endif

  // Sort the paths by their (first) labels.
  // FIXME Later: A priority queue should be faster.
  PathFirstComparator first_c(labels);
  parallelQuickSort(paths.begin(), paths.end(), first_c);
}

void
PathGraph::read(std::vector<PathNode>& paths, std::vector<PathNode::rank_type>& labels,
                size_type file, bool append) const
{
  if(!append) { sdsl::util::clear(paths); sdsl::util::clear(labels); }

  std::ifstream input; this->open(input, file);
  if(!append) { paths.reserve(this->sizes[file]); labels.reserve(this->rank_counts[file]); }
  for(size_type i = 0; i < path_count; i++) { paths.push_back(PathNode(input, labels)); }
  input.close();

#ifdef VERBOSE_STATUS_INFO
  if(!append)
  {
    std::cerr << "PathGraph::read(): Read " << paths.size() << " order-" << this->k()
              << " paths from " << this->filenames[file] << std::endl;
  }
#endif
}

//------------------------------------------------------------------------------

} // namespace gcsa
