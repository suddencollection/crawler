#include <iostream>
#include <limits>
#include <program.hpp>

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
  multi_handle = curl_multi_init();
}

Program::~Program()
{
  curl_multi_cleanup(multi_handle);
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

  curl_multi_add_handle(multi_handle, easy);

  int still_running = 0;
  curl_multi_perform(multi_handle, &still_running);

  while(still_running) {
    int numfds = 0;
    CURLMcode mc = curl_multi_wait(multi_handle, nullptr, 0, 1000, &numfds);
    if(mc != CURLM_OK) {
      curl_multi_remove_handle(multi_handle, easy);
      curl_easy_cleanup(easy);
      throw std::runtime_error("curl_multi_wait error");
    }
    curl_multi_perform(multi_handle, &still_running);
  }

  long response_code = 0;
  curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &response_code);
  if(response_code >= 400) {
    curl_multi_remove_handle(multi_handle, easy);
    curl_easy_cleanup(easy);
    throw std::runtime_error("HTTP error " + std::to_string(response_code) + " for " + url);
  }

  char* eff_url = nullptr;
  curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &eff_url);
  if(!eff_url) {
    curl_multi_remove_handle(multi_handle, easy);
    curl_easy_cleanup(easy);
    throw std::runtime_error("Invalid effective URL for " + url);
  }

  if(!is_valid_url(eff_url)) {
    throw std::runtime_error("invalid final url for " + url + ": " + eff_url);
  }

  std::string final_url{eff_url}; // copy before it gets deleted

  curl_multi_remove_handle(multi_handle, easy);
  curl_easy_cleanup(easy);

  return std::make_pair(final_url, response);
}

PageNode Program::crawl_page(std::string const& url, int depth)
{
  std::cout << "crawling page " << url << std::endl;
  if(depth <= 0 || !is_valid_url(url)) {
    return PageNode(url); // leaf node
  }

  std::string final_url;
  std::string content;

  try {
    std::tie(final_url, content) = request_html(url);
  }
  catch(const std::exception& e) {
    std::cerr << "Error fetching " << url << ": " << e.what() << '\n';
    return PageNode(url);
  }

  PageNode node(final_url);
  std::vector<PageNode> children = parse_url(final_url, content);

  node.reserve(children.size());

  std::cout << "[Depth " << depth << "]" << std::endl;
  for(const auto& child : children) {
    PageNode sub = crawl_page(child.url(), depth - 1);
    node.add(std::move(sub));
  }

  return node;
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

    if(std::cin.fail() || depth <= 0) {
      std::cin.clear();
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      message = "invalid depth, should be a positive integer: ";
    } else {
      break;
    }
  }

  return depth;
}

std::string Program::resolve_url(const std::string& base_url,
  const std::string& href)
{
  std::string out;
  CURLU* h = curl_url(); // one handle is enough
  if(!h) return out;

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
  std::vector<PageNode>& out, std::string const& base_url)
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
            std::string abs = resolve_url(base_url, raw);
            if(!abs.empty()) {
              out.emplace_back(abs);
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

std::vector<PageNode> Program::parse_url(std::string url, std::string const& content)
{
  std::vector<PageNode> pages;
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
    throw std::runtime_error(error_message + "failed to extract links.");
  }

  lxb_html_document_destroy(doc);
  return pages;
}

void Program::run()
{


  return;
  print_header(); // fancy header output
  std::string root_url = request_input();
  int depth = request_depth();
  std::cout << "Crawling pages from root " << root_url << std::endl;
  PageNode node = crawl_page(root_url, depth);
  std::cout << "Done!" << std::endl;
}
