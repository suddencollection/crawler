#include <iostream>
#include <limits>
#include <program.hpp>
#include <unordered_set>

//
#include <ogdf/basic/Graph.h>
#include <ogdf/basic/GraphAttributes.h>
#include <ogdf/fileformats/GraphIO.h>
//
#include <ogdf/energybased/FMMMLayout.h>
#include <ogdf/energybased/PivotMDS.h>

size_t Program::write_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
  std::string* buffer = static_cast<std::string*>(userdata);
  size_t total_size = size * nmemb; // size * number_of_bytes
  buffer->append(ptr, total_size);  // append exactly total_size bytes from ptr
  return total_size;
}

Program::Program()
{
  curl_global_init(CURL_GLOBAL_DEFAULT);
  m_multi_handle = curl_multi_init();
}

Program::~Program()
{
  curl_multi_cleanup(m_multi_handle);
  curl_global_cleanup();
}

Program::Response Program::request_html(std::string const& url)
{
  CURL* easy = curl_easy_init();
  if(!easy) {
    throw std::runtime_error("Failed to init curl for " + url);
  }

  std::string response;

  curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
  curl_easy_setopt(easy, CURLOPT_VERBOSE, 0L);
  curl_easy_setopt(easy, CURLOPT_TIMEOUT, 10L);       // max 10 seconds per request
  curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 5L); // max 5 seconds to connect
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, Program::write_callback);
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(easy, CURLOPT_USERAGENT, "Mozilla/5.0");

  curl_multi_add_handle(m_multi_handle, easy);

  int still_running = 0;
  curl_multi_perform(m_multi_handle, &still_running);

  while(still_running) {
    int numfds = 0;
    CURLMcode mc = curl_multi_wait(m_multi_handle, nullptr, 0, 1000, &numfds);
    if(mc != CURLM_OK) {
      curl_multi_remove_handle(m_multi_handle, easy);
      curl_easy_cleanup(easy);
      throw std::runtime_error("curl_multi_wait error");
    }
    curl_multi_perform(m_multi_handle, &still_running);
  }

  long response_code = 0;
  curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &response_code);
  if(response_code >= 400) {
    curl_multi_remove_handle(m_multi_handle, easy);
    curl_easy_cleanup(easy);
    throw std::runtime_error("HTTP error " + std::to_string(response_code) + " for " + url);
  }

  std::optional<std::string> final_url = get_effective_url(url);
  if(!final_url) {
    throw std::runtime_error("Failed to get effective_url.");
  }

  curl_multi_remove_handle(m_multi_handle, easy);
  curl_easy_cleanup(easy);
  return std::make_pair(final_url.value(), response);
}

void Program::crawl_page(std::string const& url, int depth)
{
  std::optional<std::string> effective_url = get_effective_url(url);
  if(!effective_url) {
    throw std::runtime_error("Faield to get effective url in crawl_page.");
  }

  int index = add_node(effective_url.value(), depth);
  crawl_page_rec(get_node(index), depth);
}

std::optional<std::string> Program::get_effective_url(std::string const& url)
{
  CURL* curl = curl_easy_init();
  if(!curl) {
    return std::nullopt;
  }

  curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);        // max 10 seconds per request
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L); // max 5 seconds to connect

  CURLcode res = curl_easy_perform(curl);
  char* effective_url = nullptr;
  if(res != CURLE_OK) {
    std::cerr << "CURL failed: " << curl_easy_strerror(res) << std::endl;
    return std::nullopt;
  }
  curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective_url);
  if(effective_url) {
    std::string final_url = effective_url;
    if(!is_valid_url(final_url)) {
      throw std::runtime_error("invalid final url for " + url + ": " + final_url);
    }

    curl_easy_cleanup(curl);
    return std::make_optional(final_url);
  }
  return std::nullopt;
}

std::optional<PageNode::Index> Program::crawl_page_rec(PageNode& page, int depth)
{
  URL const& url = get_url(page.index());

  // we've gone far enough
  if(depth == 0) {
    return std::nullopt;
  }

  std::cout << "crawling page " << url << std::endl;
  std::string final_url{};
  std::string content{};

  try {
    std::tie(final_url, content) = request_html(url);
  }
  catch(const std::exception& e) {
    std::cerr << "Error fetching " << url << ": " << e.what() << '\n';
    return std::nullopt;
  }

  // Building blocks
  std::unordered_set<URL> children = parse_url(final_url, content);
  get_node(page.index()).reserve(children.size());

  // children
  int ended = 0;
  for(URL const& child_url : children) {
    // avoids crawling the same page twice
    if(exists(child_url)) {
      std::cout << "duplicate " << child_url << std::endl;
      return std::nullopt;
    }

    int child_index = add_node(child_url, depth);
    page.add_link(child_index);
    auto maybe_index = crawl_page_rec(get_node(child_index), depth - 1);
    if(!maybe_index && depth == 0) {
      ++ended;
    }
  }

  if(ended != 0) {
    std::cout << ended << " have reached end for " << final_url << std::endl;
  }

  return page.index();
}

void Program::print_header()
{
  std::cout << "web crawler" << std::endl;
}

std::string Program::request_input()
{
  CURLU* parser = curl_url(); // url parser

  std::string output = "type your root url: ";
  std::string url;
  while(true) {
    std::cout << output;
    std::cin >> url;

    CURLUcode code = curl_url_set(parser, CURLUPART_URL, url.c_str(), 0);
    if(code == CURLUE_OK) {
      break;
    } else {
      output = "invalid url, try again: ";
    }
  }
  curl_url_cleanup(parser);
  return url;
}

int Program::request_depth()
{
  int depth = {};
  std::string message = "depth: ";

  while(true) {
    std::cout << message;
    std::cin >> depth;

    if(std::cin.fail() || depth < 0) {
      std::cin.clear();
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      message = "invalid depth, should be a positive integer: ";
    } else {
      break;
    }
  }

  return depth;
}

std::optional<std::string> Program::resolve_url(const std::string& base_url,
  const std::string& href)
{
  std::optional<std::string> out;
  CURLU* h = curl_url(); // one handle is enough
  if(!h) {
    throw std::runtime_error("Failed to resolve url " + base_url);
  }

  do {
    // 1) set base
    if(curl_url_set(h, CURLUPART_URL, base_url.c_str(), 0) != CURLUE_OK)
      break;

    // 2) set href allowing relative URLs
    if(curl_url_set(h, CURLUPART_URL, href.c_str(),
         CURLU_NON_SUPPORT_SCHEME) != CURLUE_OK)
      break;

    // 3) extract the resolved absolute URL
    char* full = nullptr;
    if(curl_url_get(h, CURLUPART_URL, &full, 0) == CURLUE_OK && full) {
      out = full;
      curl_free(full);
    }
  } while(false);

  curl_url_cleanup(h);
  return out;
}

// forward‑declare a small recursive helper
void Program::extract_links_rec(lxb_dom_node_t* node,
  std::unordered_set<URL>& out, std::string const& base_url)
{
  for(lxb_dom_node_t* child = node->first_child; child; child = child->next) {
    if(child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
      auto* el = lxb_dom_interface_element(child);

      // get the tag name as a C‑string
      const lxb_char_t* tag = lxb_dom_element_qualified_name(el, nullptr);
      if(tag && strcmp((const char*)tag, "a") == 0) {
        if(auto* attr = lxb_dom_element_attr_by_name(
             el, (const lxb_char_t*)"href", 4)) {
          if(auto* href = lxb_dom_attr_value(attr, nullptr)) {
            std::string raw((const char*)href);
            std::optional<Program::URL> url = resolve_url(base_url, raw);
            if(url) {
              out.insert(url.value());
            }
          }
        }
      }
    }
    extract_links_rec(child, out, base_url);
  }
}

bool Program::is_valid_url(std::string url)
{
  CURLU* h = curl_url();
  if(!h) return false;

  if(curl_url_set(h, CURLUPART_URL, url.c_str(), 0) != CURLUE_OK) {
    curl_url_cleanup(h);
    return false;
  }

  char* scheme = nullptr;
  if(curl_url_get(h, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK) {
    curl_url_cleanup(h);
    return false;
  }

  bool is_http = strcmp(scheme, "http") == 0 || strcmp(scheme, "https") == 0;
  curl_free(scheme);
  curl_url_cleanup(h);
  return is_http;
}

std::unordered_set<Program::URL> Program::parse_url(std::string url, std::string const& content)
{
  std::unordered_set<URL> pages;
  std::string error_message = "Failed to parse " + url + ". ";

  auto* doc = lxb_html_document_create();
  if(!doc) {
    throw std::runtime_error(error_message + "lxb_html_document_create() error.");
  }

  auto parse_result = lxb_html_document_parse(doc, reinterpret_cast<const lxb_char_t*>(content.c_str()), content.size());
  if(parse_result != LXB_STATUS_OK) {
    lxb_html_document_destroy(doc);
    throw std::runtime_error(error_message + "lxb_html_document_parse() error.");
  }

  auto* body = lxb_html_document_body_element(doc);
  if(body == nullptr) {
    throw std::runtime_error(error_message + "lxb_html_document_parse() error.");
  }

  extract_links_rec(lxb_dom_interface_node(body), pages, url);
  if(pages.empty()) {
    std::cerr << error_message << "failed to extract links." << std::endl;
  }

  lxb_html_document_destroy(doc);
  return pages;
}

auto Program::get_node(PageNode::Index index) -> PageNode&
{
  return m_nodes.at(index);
}

auto Program::exists(std::string const& url) -> bool
{
  return m_url_to_index.find(url) != m_url_to_index.end();
}

auto Program::add_node(std::string const& url, int depth) -> PageNode::Index
{
  auto it = m_url_to_index.find(url);
  if(it == m_url_to_index.end()) {
    int index = m_nodes.size();
    m_index_to_url[index] = url;
    m_url_to_index[url] = index;
    m_nodes.push_back(PageNode(index, depth));

    return index;
  }

  return it->second;
}

void Program::graph()
{
  // create graph
  ogdf::Graph G; // creates the empty graph

  // create nodes
  std::vector<ogdf::node> nodes;
  nodes.reserve(m_nodes.size());
  for(int i = 0; i < m_nodes.size(); ++i) {
    nodes.push_back(G.newNode()); // Adds 50 nodes to G
  }

  // create edges
  for(std::size_t i = 0; i < m_nodes.size(); ++i) {
    PageNode const& node = m_nodes[i];
    if(node.children().empty() ) {
      std::cout << "empty node children " << node.index() << std::endl;
    }

    for(int child_index : node.children()) {
      if(child_index >= 0 && child_index < static_cast<int>(nodes.size())) {
        G.newEdge(nodes[i], nodes[child_index]);
      }
    }
  }

  std::cout << "[graph] Nodes: " << G.numberOfNodes()
            << ", Edges: " << G.numberOfEdges() << "\n";

  // 2) Set up attributes & initial positions
  ogdf::GraphAttributes GA(G, ogdf::GraphAttributes::nodeGraphics |
                                ogdf::GraphAttributes::edgeGraphics |
                                ogdf::GraphAttributes::nodeStyle |
                                ogdf::GraphAttributes::edgeStyle);

  std::mt19937 gen{std::random_device{}()};
  std::uniform_real_distribution<> coord(-200.0, 200.0);
  for(auto v : G.nodes) {
    GA.x(v) = coord(gen);
    GA.y(v) = coord(gen);
    GA.width(v) = 3.0;
    GA.height(v) = 3.0;
    GA.shape(v) = ogdf::Shape::Ellipse;
  }

  // 3) Run the one layout engine you included
  // ──────────────────────────────────────────
  ogdf::FMMMLayout layout; // <— swap this line + include above as needed
  layout.call(GA);
  ogdf::GraphIO::write(GA,
    "graph-FMMMLayout.svg",
    ogdf::GraphIO::drawSVG);

  std::cout << "Wrote graph-FMMMLayout.svg\n";
}

void Program::run()
{
  print_header(); // fancy header output
  // std::string root_url = request_input();
  // int depth = request_depth();

  std::string root_url = "https://www.iana.org/help/example-domains";
  int depth = 1;

  std::cout << "Crawling pages from root " << root_url << std::endl;

  crawl_page(root_url, depth);

  std::cout << "Done! node_count: " << m_nodes.size() << std::endl;
  std::cout << "index_to_url " << m_index_to_url.size() << std::endl;
  std::cout << "url_to_index " << m_url_to_index.size() << std::endl;
  std::cout << "m_nodes      " << m_nodes.size() << std::endl;

  graph();
}
