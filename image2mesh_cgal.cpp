#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
//#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
// #include <CGAL/Exact_predicates_exact_constructions_kernel_with_sqrt.h>

// #include <CGAL/Cartesian.h>
// #include <CGAL/MP_Float.h>
// #include <CGAL/Lazy_exact_nt.h>
// #include <CGAL/Quotient.h>
// #include <CGAL/Gmpq.h>

#include <CGAL/Mesh_triangulation_3.h>
#include <CGAL/Mesh_complex_3_in_triangulation_3.h>
#include <CGAL/Mesh_criteria_3.h>
#include <CGAL/Mesh_constant_domain_field_3.h>

#include <CGAL/Labeled_image_mesh_domain_3.h>
#include <CGAL/make_mesh_3.h>
#include <CGAL/Image_3.h>
#include <CGAL/refine_mesh_3.h>
#include <CGAL/ImageIO.h>

#include <vector>
#include <map>
#include <algorithm>
#include <set>

#ifdef _OPENMP
#include <omp.h>
#endif
#include <inttypes.h>
#define __STDC_FORMAT_MACROS

#include <json/json.h>

// Domain
typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
// typedef CGAL::Exact_predicates_exact_constructions_kernel_with_sqrt K;
// typedef CGAL::Lazy_exact_nt<CGAL::Gmpq > NT;
// typedef CGAL::Cartesian<NT> K;

typedef CGAL::Labeled_image_mesh_domain_3<CGAL::Image_3,K> Mesh_domain;
typedef Mesh_domain::Index Index;
// Triangulation
typedef CGAL::Mesh_triangulation_3<Mesh_domain>::type Tr;
typedef CGAL::Mesh_complex_3_in_triangulation_3<Tr> C3t3;

// Criteria
typedef CGAL::Mesh_criteria_3<Tr> Mesh_criteria;
typedef CGAL::Mesh_constant_domain_field_3<Mesh_domain::R,
                                           Mesh_domain::Index> Sizing_field;
typedef Mesh_criteria::Facet_criteria FacetCriteria;
typedef Mesh_criteria::Cell_criteria CellCriteria;

typedef Mesh_domain::Point_3 Point_3;
struct PointType
{
    double x;
    double y;
    double z;
};
// typedef Mesh_domain::Point_3 shit;

// To avoid verbose function and named parameters call
using namespace CGAL::parameters;
using std::min;
using std::max;

void create_mesh_no_refinement(C3t3& mesh, Mesh_domain& domain,
            CGAL::parameters::internal::Exude_options& exude_setting,
            CGAL::parameters::internal::Perturb_options& perturb_setting,
            CGAL::parameters::internal::Lloyd_options& lloyd_options,
            CGAL::parameters::internal::Odt_options& odt_options);

void refine_mesh_with_inserted_steiner_points(C3t3& mesh, Mesh_domain& domain,
            CGAL::Image_3 &image,
            CGAL::parameters::internal::Exude_options& exude_setting,
            CGAL::parameters::internal::Perturb_options& perturb_setting,
            CGAL::parameters::internal::Lloyd_options& lloyd_options,
            CGAL::parameters::internal::Odt_options& odt_options);

void refine_mesh_use_std_method(C3t3& mesh, Mesh_domain& domain,
            CGAL::parameters::internal::Exude_options& exude_setting,
            CGAL::parameters::internal::Perturb_options& perturb_setting,
            CGAL::parameters::internal::Lloyd_options& lloyd_options,
            CGAL::parameters::internal::Odt_options& odt_options);

// Usage: image2mesh_cgal inputstack.inr criteria.txt
// criterial.txt is a text file containing setting for mesh sizes and refirement options.

static std::string readInputFile( const char *path ) {
   FILE *file = fopen( path, "rb" );
   if ( !file )
      return std::string("");

   fseek( file, 0, SEEK_END );
   long size = ftell( file );
   fseek( file, 0, SEEK_SET );

   std::string text;
   char *buffer = new char[size+1];
   buffer[size] = 0;

   if ( fread( buffer, 1, size, file ) == (unsigned long)size )
      text = buffer;
   fclose( file );

   delete[] buffer;
   return text;
}

template<typename Mesh_domain, typename OutputIterator>
void ConstructSeedPoints(const CGAL::Image_3& image, const Mesh_domain* domain, const std::map<int, double>& lengths, OutputIterator pts) {

    double origin[3] = {0., 0., 0.};
    double endPoint[3] = {image.vx()*image.xdim(), image.vy()*image.ydim(), image.vz()*image.zdim()};

    //std::set<int> foo;
    uint64_t counter = 0;
    for (std::map<int, double>::const_iterator iter = lengths.begin();
         iter != lengths.end(); ++iter)
    {
        const int current_label = iter->first;
        const double current_size = iter->second;

        int xCount = static_cast<int>( (endPoint[0] - origin[0]) / current_size );
        int num_threads = 1;
        int thread_num = 0;
        #ifdef _OPENMP
        num_threads = omp_get_max_threads();
        thread_num = omp_get_thread_num();
        #endif
        std::set<int> _labels;
        std::vector<std::vector<std::pair<PointType,int> > > seedPoints(num_threads);
        for (std::size_t i = 0; i < seedPoints.size(); ++i) {
          seedPoints[i].reserve(1000);
        }
        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (int i = 0; i < xCount; ++i) {
        	double seedPointCandidate[3] = {origin[0] + i * current_size, 0., 0.};
            while (seedPointCandidate[1] < endPoint[1]) {
                seedPointCandidate[2] = origin[2];
                while (seedPointCandidate[2] < endPoint[2]) {
                    uint64_t label = image.labellized_trilinear_interpolation(
                            seedPointCandidate[0], seedPointCandidate[1],
                            seedPointCandidate[2],(unsigned char)(0));
                    if (label != 0 && current_label == label) {
                        PointType foo;
                        foo.x = seedPointCandidate[0];
                        foo.y = seedPointCandidate[1];
                        foo.z = seedPointCandidate[2];
                        seedPoints[thread_num].push_back(std::make_pair(foo, label));
                        ++counter;
                        _labels.insert(label);
                    }
                    seedPointCandidate[2] += current_size;
                }
                seedPointCandidate[1] += current_size;
            }
        }
        std::cout << " ..Mesh needs a total of " << counter <<
            " Steiner points to meet sizing requirements." << std::endl;
        std::cout.flush();
        std::cout << " ..labels refined are: ";
        for (std::set<int>::iterator i=_labels.begin(); i != _labels.end(); ++i)
        {
            printf(", %d", *i);
        }
        printf("\n");
        for (std::size_t i = 0; i < seedPoints.size(); ++i) {
            for (std::size_t j = 0; j < seedPoints[i].size(); ++j) {
                const PointType seedPoint = seedPoints[i][j].first;
                *pts++ = std::make_pair(Point_3(seedPoint.x, seedPoint.y,
                                        seedPoint.z),
                domain->index_from_subdomain_index(seedPoints[i][j].second));
            }
        }
    }
}

std::string inrfn="cgal_mesher.inr", outfn="out.mesh";
double my_facet_angle=25., my_facet_size=2., my_facet_distance=1.5,
       my_cell_radius_edge=3., my_general_cell_size=2., sliver_angle_bound=15.0;
std::map<int, double> region2size;
bool do_refinement=false, do_optimization=false,
     do_sliver=true, do_perturb=true,
     opt_method_lloyd=false, opt_method_odt = false;
bool keep_detailed_features=false;
int opt_time_limit=0, sliver_time_limit=300, perturb_time_limit=0;
int volume_dimension = 3;

int parse_config_file(const char *config_fn)
{
    Json::Value root;
    Json::Reader reader;

    std::string input = readInputFile(config_fn);
    if (input.empty()) {
        std::cout << "\n\n...Can't read meshing configuration file: "
                  << std::string(config_fn) << std::endl;
        return 1;
    }
    bool parsingOK = reader.parse(input, root);
    if ( !parsingOK ) {
        std::cout << " Failed to parse input configuration: "
                  << '"' << std::string(config_fn) << "\"\n"
                  << reader.getFormattedErrorMessages()
                  << std::endl << std::endl;
        return 1;
    }

    inrfn = root.get("inrfilename", "._cgal_mesher.inr").asString();
    outfn = root.get("outfilename", "._out.mesh").asString();
    Json::Value facet_setting        = root[ "facet_settings" ];
    Json::Value cell_setting         = root[ "cell_settings" ];
    Json::Value refine_setting       = root[ "refinement" ];
    Json::Value post_process_setting = root[ "post_processing" ];

    my_facet_size = 3.0; my_facet_angle = 25.0; my_facet_distance = 2.0;
    my_cell_radius_edge = 3.0; my_general_cell_size = 3.0;
    do_refinement = false;
    do_optimization = false; opt_time_limit = 0;
    opt_method_lloyd = false; opt_method_odt = false;
    do_sliver = true; sliver_angle_bound = 15.0; sliver_time_limit = 300;
    do_perturb = true; perturb_time_limit = 0;
    keep_detailed_features = false;

    if ( facet_setting.type() != Json::nullValue) {
        my_facet_size      = facet_setting.get("size", 3.0).asDouble();
        my_facet_angle     = facet_setting.get("angle", 25.0).asDouble();
        my_facet_distance  = facet_setting.get("distance", 2.0).asDouble();
    }

    if ( cell_setting.type() != Json::nullValue ) {
        my_general_cell_size  = cell_setting.get("size", 3.0).asDouble();
        my_cell_radius_edge   = cell_setting.get("edge_radius_ratio", 3.0).asDouble();
    }

    if (refine_setting != Json::nullValue ) {
        const Json::Value region_ids    = refine_setting[ "region_ids" ];
        const Json::Value region_sizes  = refine_setting[ "region_sizes" ];
        keep_detailed_features          = refine_setting.get("keep_detailed_features", false).asBool();

        for (int index = 0; index < region_ids.size(); ++index) {
            region2size[region_ids[index].asInt()] = region_sizes[index].asDouble();
        }
        std::cout << region2size.size() << '\n';
        if ( (region2size.size() == 1 && region2size.count(0) > 0) ||
            region2size.empty() ) {
            do_refinement = false;
            region2size.clear();
        }
        else {
            do_refinement = true;
        }
        std::cout << region2size.size() << '\n';
        // Make sure no label with ID == 0 exists
        std::map<int, double>::iterator it = region2size.find(0);
        if (it != region2size.end()) {
            region2size.erase(it);
        }
        // TODO: after reading image, if region2size has only 1 element
        //       and the 'image' only contains one region ID, then
        //       'refinement' doesn't make sense and we should follow the
        //       the global cell size given.
        // if (region2size.size() == 1)
        //    do_refinement = false;

        std::cout << region2size.size() << '\n';
        keep_detailed_features = keep_detailed_features
                                && do_refinement && !region2size.empty();
    }

    if ( post_process_setting != Json::nullValue ) {
        Json::Value foo = post_process_setting["optimization"];
        if ( foo != Json::nullValue ) {
            opt_method_odt      = foo.get("odt", false).asBool();
            opt_method_lloyd      = foo.get("lloyd", false).asBool();
            opt_time_limit  = foo.get("time_limit", 0).asInt();
            if (opt_method_lloyd || opt_method_odt)
                do_optimization = true;
            else
                do_optimization = false;
        }
        foo = post_process_setting["sliver_exude"];
        if (foo != Json::nullValue ) {
            do_sliver           = foo.get("perform", true).asBool();
            sliver_angle_bound  = foo.get("angle_bound", 15.0).asDouble();
            sliver_time_limit   = foo.get("time_limit", 300).asInt();
        }
        foo = post_process_setting["perturb_mesh"];
        if (foo != Json::nullValue) {
            do_perturb          = foo.get("perform", true).asBool();
            perturb_time_limit  = foo.get("time_limit", 0).asInt();
        }
    }
    return 0;
}
int main(int argc, char *argv[])
{
	// Loads image
	CGAL::Image_3 image;

    const char *config_fn;

    if (argc == 1) {
        std::cout << " Enter the meshing configuration filename (.json): ";
        std::cin >> inrfn;
        if (inrfn.empty()) {
            std::cout << "\n(Using default settings for meshing parameters!)\n";
            config_fn = "config.json";
        }
        else
            config_fn = inrfn.c_str();
    } else
        config_fn = argv[1];

    int parse_st = parse_config_file(config_fn);

    std::cout << "my_facet_size: " << my_facet_size << std::endl;
    std::cout << "my_facet_angle: " << my_facet_angle << std::endl;
    std::cout << "my_facet_distance: " << my_facet_distance << std::endl;
    std::cout << "my_cell_radius_edge: " << my_cell_radius_edge << std::endl;
    std::cout << "my_general_cell_size: " << my_general_cell_size << std::endl;
    std::cout << "do_refinement: " << (do_refinement ? "yes" : "no") << std::endl;
    std::cout << "do_optimization: " << (do_optimization ? "yes" : "no") << std::endl;
    std::cout << "opt_time_limit: " << opt_time_limit << std::endl;
    std::cout << "opt_method_odt: " << opt_method_odt << std::endl;
    std::cout << "opt_method_lloyd: " << opt_method_lloyd << std::endl;
    std::cout << "do_sliver: " << (do_sliver ? "yes" : "no") << std::endl;
    std::cout << "sliver_angle_bound: " << sliver_angle_bound << std::endl;
    std::cout << "sliver_time_limit: " << sliver_time_limit << std::endl;
    std::cout << "do_perturb: " << (do_perturb ? "yes" : "no") << std::endl;
    std::cout << "perturb_time_limit: " << perturb_time_limit << std::endl;
    std::cout << "keep_detailed_features: " << (keep_detailed_features ? "yes" : "no") << std::endl;
    std::cout << "inrfilename: " << inrfn << std::endl;
    std::cout << "outfn: " << outfn << std::endl;

    if (parse_st != 0) {
        std::cout << "\tExiting!\n\n";
        exit(1);
    }
    bool ret = image.read(inrfn.c_str());
    if (ret) {
        std::cout << " Read the image successfully\n";
    }

    // Domain
    Mesh_domain domain(image);

    C3t3 mesh;

    CGAL::parameters::internal::Exude_options exude_setting(true);
    CGAL::parameters::internal::Perturb_options perturb_setting(true);
    CGAL::parameters::internal::Lloyd_options lloyd_options(false);
    CGAL::parameters::internal::Odt_options odt_options(false);

    // Sliver removal setting
    if (do_sliver)
        exude_setting =
            CGAL::parameters::exude(time_limit=0,
                                    sliver_bound=sliver_angle_bound);
    else
        exude_setting = CGAL::parameters::no_exude();
    // Perturbation setting
    if (do_perturb)
        perturb_setting = CGAL::parameters::perturb(perturb_time_limit);
    else
        perturb_setting = CGAL::parameters::no_perturb();

    // Optimization settting
    if (do_optimization) {
        if (opt_method_odt) {
            odt_options = CGAL::parameters::odt(time_limit = opt_time_limit);
        }
        else
            odt_options = CGAL::parameters::no_odt();
        if (opt_method_lloyd) {
            lloyd_options = CGAL::parameters::lloyd(time_limit = opt_time_limit);
        }
        else
            lloyd_options = CGAL::parameters::no_lloyd();
    }
    else {
        lloyd_options = CGAL::parameters::no_lloyd();
        odt_options = CGAL::parameters::no_odt();
    }

    if (do_refinement) {
        if (keep_detailed_features) {
            refine_mesh_with_inserted_steiner_points(mesh, domain, image,
                                                exude_setting, perturb_setting,
                                                lloyd_options, odt_options);
        }
        else {
            refine_mesh_use_std_method(mesh, domain, exude_setting,
                                              perturb_setting,
                                              lloyd_options, odt_options);
        }
    }
    else {
        create_mesh_no_refinement(mesh, domain, exude_setting,
                                         perturb_setting,
                                         lloyd_options, odt_options);
    }

    std::ofstream medit_file(outfn.c_str());
    mesh.output_to_medit(medit_file);

    return 0;
}
void refine_mesh_use_std_method(C3t3& mesh, Mesh_domain& domain,
            CGAL::parameters::internal::Exude_options& exude_setting,
            CGAL::parameters::internal::Perturb_options& perturb_setting,
            CGAL::parameters::internal::Lloyd_options& lloyd_options,
            CGAL::parameters::internal::Odt_options& odt_options) {

    Sizing_field size(my_general_cell_size);
    Sizing_field facetRadii(my_general_cell_size);
    std::map<int,double>::const_iterator it;

    for (it=region2size.begin(); it!=region2size.end(); ++it) {
        size.set_size(it->second, volume_dimension,
                    domain.index_from_subdomain_index(it->first));
        facetRadii.set_size(it->second, volume_dimension,
                    domain.index_from_subdomain_index(it->first));
    }

    FacetCriteria facet_criteria(my_facet_angle, facetRadii, my_facet_distance);
    CellCriteria cell_criteria(my_cell_radius_edge, size);
    Mesh_criteria mesh_criteria(facet_criteria, cell_criteria);

    mesh = CGAL::make_mesh_3<C3t3>(domain, mesh_criteria, lloyd_options,
                        odt_options, perturb_setting, exude_setting);
}

void refine_mesh_with_inserted_steiner_points(C3t3& mesh, Mesh_domain& domain,
            CGAL::Image_3& image,
            CGAL::parameters::internal::Exude_options& exude_setting,
            CGAL::parameters::internal::Perturb_options& perturb_setting,
            CGAL::parameters::internal::Lloyd_options& lloyd_options,
            CGAL::parameters::internal::Odt_options& odt_options) {

    Sizing_field size(my_general_cell_size);
    Sizing_field facetRadii(my_general_cell_size);
    std::map<int,double>::const_iterator it;

    for (it=region2size.begin(); it!=region2size.end(); ++it) {
        size.set_size(it->second, volume_dimension,
                    domain.index_from_subdomain_index(it->first));
        facetRadii.set_size(it->second, volume_dimension,
                    domain.index_from_subdomain_index(it->first));
    }

	FacetCriteria facet_criteria(my_facet_angle, facetRadii, my_facet_distance);
	CellCriteria cell_criteria(my_cell_radius_edge, size);
	Mesh_criteria mesh_criteria(facet_criteria, cell_criteria);

	typedef std::vector<std::pair<Point_3, Index> > initial_points_vector;
	typedef initial_points_vector::iterator Ipv_iterator;
	typedef C3t3::Vertex_handle Vertex_handle;

	initial_points_vector initial_points;
	ConstructSeedPoints(image, &domain, region2size,
                        std::back_inserter(initial_points));

	if (initial_points.empty()) {
		domain.construct_initial_points_object()(
                                            std::back_inserter(initial_points));
		volume_dimension = 3;
	}
    std::cout << " ..inserting nodes\n";
    std::cout.flush();

	for (Ipv_iterator it=initial_points.begin(); it!=initial_points.end(); ++it) {
		Vertex_handle v = mesh.triangulation().insert(it->first);
		if (v != Vertex_handle()) {
			mesh.set_dimension(v, volume_dimension);
			mesh.set_index(v, it->second);
		}
	}

	CGAL_assertion(mesh.triangulation().dimension() == 3);
    std::cout << " ..generating mesh\n";
    std::cout.flush();

	CGAL::refine_mesh_3(mesh, domain, mesh_criteria,
            CGAL::parameters::no_reset_c3t3(), lloyd_options, odt_options,
            perturb_setting, exude_setting);

    // std::cout << " ..optimizing mesh\n";
    // std::cout.flush();
    // CGAL::odt_optimize_mesh_3(mesh, domain, time_limit=0);

    // std::cout << " ..perturbing mesh\n";
    // std::cout.flush();
    // CGAL::perturb_mesh_3(mesh, domain, time_limit=0);

    // std::cout << " ..exuding sliver elements\n";
    // std::cout.flush();
    // CGAL::exude_mesh_3(mesh, sliver_bound=15, time_limit=300);

    // std::cout << " ..writing mesh\n";
    // std::cout.flush();
    // std::ofstream medit_file(outfn.c_str());
    // mesh.output_to_medit(medit_file);

}

void create_mesh_no_refinement(C3t3& mesh, Mesh_domain& domain,
            CGAL::parameters::internal::Exude_options& exude_setting,
            CGAL::parameters::internal::Perturb_options& perturb_setting,
            CGAL::parameters::internal::Lloyd_options& lloyd_options,
            CGAL::parameters::internal::Odt_options& odt_options) {

		// Mesh criteria
		Mesh_criteria criteria(facet_angle=my_facet_angle,
                                facet_size=my_facet_size,
                                facet_distance=my_facet_distance,
		                        cell_radius_edge=my_cell_radius_edge,
                                cell_size=my_general_cell_size);

		// Meshing
		mesh = CGAL::make_mesh_3<C3t3>(domain, criteria, lloyd_options,
                        odt_options, perturb_setting, exude_setting);
}
