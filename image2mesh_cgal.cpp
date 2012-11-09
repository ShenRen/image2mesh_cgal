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
// typedef std::vector<double> PointType;
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
// Usage: image2mesh_cgal inputstack.inr criteria.txt
// criterial.txt is a text file containing setting for mesh sizes and refirement options.

template<typename Mesh_domain, typename OutputIterator>
void ConstructSeedPoints(const CGAL::Image_3& image, const Mesh_domain* domain, const std::map<int, double>& lengths, OutputIterator pts) {

    // typedef typename Mesh_domain::Point_3 PointType;

    // Point_3 origin = image.GetGeometry()->GetOrigin();
    // Point_3 endPoint = image.GetGeometry()->GetCornerPoint(7);
//    PointType origin; origin.push_back(0.); origin.push_back(0.); origin.push_back(0.);
    double origin[3] = {0., 0., 0.};
//    PointType endPoint;
//    endPoint.push_back(image.vx()*image.xdim());
//    endPoint.push_back(image.vy()*image.ydim());
//    endPoint.push_back(image.vz()*image.zdim());
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
                            seedPointCandidate[0], seedPointCandidate[1], seedPointCandidate[2],(unsigned char)(0));
                    if (label != 0 && current_label == label) {
                        PointType foo;
                        foo.x = seedPointCandidate[0]; foo.y = seedPointCandidate[1]; foo.z = seedPointCandidate[2];
                        seedPoints[thread_num].push_back(std::make_pair(foo, label));
                        ++counter;
                        _labels.insert(label);
                    }
                    seedPointCandidate[2] += current_size;
                }
                seedPointCandidate[1] += current_size;
            }
        }
        std::cout << " ..Mesh needs a total of " << counter << " Steiner points to meet sizing requirements." << std::endl;
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
                *pts++ = std::make_pair(Point_3(seedPoint.x, seedPoint.y, seedPoint.z), domain->index_from_subdomain_index(seedPoints[i][j].second));
            }
        }
    }
}

int main(int argc, char *argv[])
{
	// Loads image
	CGAL::Image_3 image;
	std::string cfn, inrfn, outfn;
	double facet_angle=25, facet_size=2, facet_distance=1.5,
	       cell_radius_edge=2, general_cell_size=2;
	double special_size = 0.9; // Cell size to be used in subdomains of image with 'special_subdomain_label'
	int special_subdomain_label = 0; // If this is set to zero, no subdomain resizing will be performed
	int volume_dimension = 3;

	bool defulatcriteria = false;

	if (argc == 1) {
		std::cout << " Enter the image stack file name (.inr): ";
		std::cin >> inrfn;
		defulatcriteria = true;
		std::cout << " (Using default settings for meshing parameters!)\n";
	}
	else if (argc==2) {
		inrfn = argv[1];
		defulatcriteria = true;
	}
	else if (argc >= 3) {
		inrfn = argv[1];
		cfn = argv[2];
	}
	if (!defulatcriteria) {
		std::ifstream cfs(cfn.c_str());
		if (!cfs) {
			std::cerr << " Can not read mesh criteria file!\n";
			exit(-1);
		}
		cfs >> facet_angle;
		cfs >> facet_size;
		cfs >> facet_distance;
		cfs >> cell_radius_edge;
		cfs >> general_cell_size;
		cfs >> special_subdomain_label;
		cfs >> special_size;
	}
	if (argc >= 4)
		outfn = argv[3];
	else
		outfn = "_tmp_image2mesh_cgal.mesh";

	bool ret = image.read(inrfn.c_str());
    if (ret) {
        std::cout << " Read the image successfully\n";
    }

	// Domain
	Mesh_domain domain(image);

	// Sizing field: set global size to general_cell_size
	// and special size (label special_subdomain_label) to special_size
	Sizing_field size(general_cell_size);
	if (special_subdomain_label) {
		std::cout << " Refining domain with label ID: " << special_subdomain_label << std::endl;
        std::cout.flush();
		size.set_size(special_size, volume_dimension,
		              domain.index_from_subdomain_index(special_subdomain_label));
		Sizing_field facetRadii(general_cell_size);
		facetRadii.set_size(special_size, volume_dimension,
		                    domain.index_from_subdomain_index(special_subdomain_label));
		FacetCriteria facet_criteria(facet_angle, facetRadii, facet_distance);
		CellCriteria cell_criteria(cell_radius_edge, size);
		Mesh_criteria mesh_criteria(facet_criteria, cell_criteria);

		typedef std::vector<std::pair<Point_3, Index> > initial_points_vector;
		typedef initial_points_vector::iterator Ipv_iterator;
		typedef C3t3::Vertex_handle Vertex_handle;

		// Initialize c3t3
  		C3t3 c3t3;

		std::map<int, double> subdomain_label_size;
		subdomain_label_size.insert(std::make_pair(special_subdomain_label, special_size));

		initial_points_vector initial_points;
		ConstructSeedPoints(image, &domain, subdomain_label_size, std::back_inserter(initial_points));

		if (initial_points.empty()) {
			domain.construct_initial_points_object()(std::back_inserter(initial_points));
			volume_dimension = 3;
		}
        std::cout << " ..inserting nodes\n";
        std::cout.flush();

		for (Ipv_iterator it=initial_points.begin(); it!=initial_points.end(); ++it) {
			Vertex_handle v = c3t3.triangulation().insert(it->first);
			if (v != Vertex_handle()) {
				c3t3.set_dimension(v, volume_dimension);
				c3t3.set_index(v, it->second);
			}
		}
		CGAL_assertion(c3t3.triangulation().dimension() == 3);
        std::cout << " ..generating mesh\n";
        std::cout.flush();
		CGAL::refine_mesh_3(c3t3, domain, mesh_criteria, CGAL::parameters::no_reset_c3t3(), no_perturb(), no_exude());

        std::cout << " ..optimizing mesh\n";
        std::cout.flush();
        CGAL::odt_optimize_mesh_3(c3t3, domain, time_limit=0);

        std::cout << " ..perturbing mesh\n";
        std::cout.flush();
        CGAL::perturb_mesh_3(c3t3, domain, time_limit=0);

        std::cout << " ..exuding sliver elements\n";
        std::cout.flush();
        CGAL::exude_mesh_3(c3t3, sliver_bound=15, time_limit=300);

        std::cout << " ..writing mesh\n";
        std::cout.flush();
		std::ofstream medit_file(outfn.c_str());
		c3t3.output_to_medit(medit_file);
	}
	else {
		// Mesh criteria
		Mesh_criteria criteria(facet_angle, facet_size, facet_distance,
		                       cell_radius_edge, cell_size=size);

		// Meshing
		C3t3 c3t3 = CGAL::make_mesh_3<C3t3>(domain, criteria);


        std::cout << " ..optimizing mesh\n";
        std::cout.flush();
        CGAL::odt_optimize_mesh_3(c3t3, domain, time_limit=0);

		// Output
		std::ofstream medit_file(outfn.c_str());
		c3t3.output_to_medit(medit_file);
	}

	return 0;
}
