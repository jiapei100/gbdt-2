#include "gbdt.h"
#include "node.h"
#include <rapidjson/document.h>
#include <rapidjson/filestream.h>
#include <rapidjson/writer.h>
#include <assert.h>
#include <stdlib.h>
#include <algorithm>
#include <limits>
#include <list>

#if defined USE_10000_RANDOM
// If we want to get deterministic random number sequence,
// consider turn on macro "USE_10000_RANDOM".
class Rand01
{
private:
    static float data_[10000];
    const float rate_;
    size_t i_;

public:
    explicit Rand01(double rate)
        : rate_((float)rate), i_(0) {}

    bool is_one()
    {
        i_ = (i_+1) % 10000;
        return data_[i_] < rate_;
    }
};

float Rand01::data_[10000] = {
#include "10000float.inl"
};
#else
class Rand01
{
private:
    const int threshold_1_;
    unsigned seed_;

public:
    explicit Rand01(double rate_1)
        : threshold_1_((int)(rate_1 * RAND_MAX)), seed_(0) {}

    bool is_one()
    {
#if defined _WIN32
        int i = rand();
#else
        int i = rand_r(&seed_);
#endif
        return i < threshold_1_;
    }
};
#endif

class TreeLossNode : public TreeNode<TreeLossNode>
{
private:
    // in current solution, response and residual are identical,
    // it is put here for future usage.
    std::vector<double> response_;
    // residual for only this tree node.
    // NOTE: even for a root node,
    // it is not the residual for the whole tree,
    // but only the root node.
    std::vector<double> residual_;

    // loss of current tree and all preceding trees
    double total_loss_;
    // loss of current split
    double loss_;

private:
    TreeLossNode(const TreeParam& param, size_t level)
        : TreeNode<TreeLossNode>(param, level),
        response_(), residual_(),
        total_loss_(0.0), loss_(0.0) {}

    void add_xy_set_residuals(const XYSet& full_set, const std::vector<double>& full_residual)
    {
        set().load(full_set);
        residual_ = full_residual;
    }

    void add_xy_residual(const XY& xy, double r)
    {
        set().add(xy);
        residual_.push_back(r);
    }

    static TreeLossNode * create_root(const XYSet& full_set, const TreeParam& param,
        const std::vector<double>& full_residual)
    {
        TreeLossNode * root = new TreeLossNode(param, 0);
        root->left() = 0;
        root->right() = 0;
        root->leaf() = false;

        XYSetRef& root_xy_set = root->set();
        if (param.gbdt_sample_rate >= 1.0)
        {
            root->add_xy_set_residuals(full_set, full_residual);
        }
        else
        {
            // sample 'full_set' and 'full_residual' together
            root_xy_set.spec() = &full_set.spec();
            Rand01 r(param.gbdt_sample_rate);
            for (size_t i=0, s=full_set.size(); i<s; i++)
            {
                if (r.is_one())
                    root->add_xy_residual(full_set.get(i), full_residual[i]);
            }
        }
        assert(root_xy_set.get_xtype_size() != 0);
        assert(root_xy_set.size() != 0);

        root->residual_2_response();

        return root;
    }

    static void split(TreeLossNode * parent)
    {
        XYSetRef& parent_xy_set = parent->set();
        double y_left = 0.0;
        double y_right = 0.0;
        parent->loss_all(&parent->split_x_index(),
            &parent->split_x_type(),
            &parent->split_x_value(),
            &y_left,
            &y_right,
            &parent->loss_);

        TreeLossNode * _left = new TreeLossNode(parent->param(), parent->level() + 1);
        _left->left() = 0;
        _left->right() = 0;
        _left->set().spec() = parent_xy_set.spec();
        _left->leaf() = false;
        _left->y() = y_left;

        TreeLossNode * _right = new TreeLossNode(parent->param(), parent->level() + 1);
        _right->left() = 0;
        _right->right() = 0;
        _right->set().spec() = parent_xy_set.spec();
        _right->leaf() = false;
        _right->y() = y_right;

        parent->left() = _left;
        parent->right() = _right;

        size_t _split_x_index = parent->split_x_index();
        if (parent->split_is_numerical())
        {
            double split_x_value_double = parent->split_get_double();
            for (size_t i=0, s=parent_xy_set.size(); i<s; i++)
            {
                double residual = parent->residual_[i];
                const XY& xy = parent_xy_set.get(i);
                double x = xy.x(_split_x_index).d();
                if (x <= split_x_value_double)
                    _left->add_xy_residual(xy, residual);
                else
                    _right->add_xy_residual(xy, residual);
            }
        }
        else
        {
            double split_x_value_int = parent->split_get_int();
            for (size_t i=0, s=parent_xy_set.size(); i<s; i++)
            {
                double residual = parent->residual_[i];
                const XY& xy = parent_xy_set.get(i);
                int x = xy.x(_split_x_index).i();
                if (x == split_x_value_int)
                    _left->add_xy_residual(xy, residual);
                else
                    _right->add_xy_residual(xy, residual);
            }
        }
        _left->residual_2_response();
        _right->residual_2_response();
        assert(parent->set().size() ==
            _left->set().size() + _right->set().size());
    }

    static void train(TreeLossNode * root)
    {
        const TreeParam& param = root->param();
        std::list<TreeLossNode *> stack;
        stack.push_back(root);
        size_t leaf_size = 0;

        while (!stack.empty())
        {
            TreeLossNode * node = stack.back();
            stack.pop_back();

            size_t level = node->level();
            double positive = node->set().positive();
            double negative = node->set().negative();
            double total = positive + negative;
            if (level >= param.max_level
                || leaf_size >= param.max_leaf_number
                || std::max(positive, negative) / total > param.leaf_threshold)
            {
                node->leaf() = true;
                leaf_size++;
                continue;
            }

            split(node);
            stack.push_back(node->left());
            stack.push_back(node->right());
        }

        root->shrink();
    }

    static void __shrink(TreeLossNode * node, double rate)
    {
        if (node->is_leaf())
            node->y() *= rate;
        else
        {
            TreeLossNode * left = node->left();
            assert(left);
            __shrink(left, rate);
            TreeLossNode * right = node->right();
            assert(right);
            __shrink(right, rate);
        }
    }

    void shrink()
    {
        if (param().gbdt_learning_rate >= 1.0)
            return;
        __shrink(this, param().gbdt_learning_rate);
    }

    void residual_2_response()
    {
        assert(response_.empty());
        // residual and response are identical
        response_ = residual_;// a deep copy
    }

    void update_residual(const XYSet& full_set, std::vector<double> * full_residual) const
    {
        for (size_t i=0, s=full_set.size(); i<s; i++)
        {
            const XY& xy = full_set.get(i);
            (*full_residual)[i] -= predict(xy.X());
        }
    }

    void drain()
    {
        set().clear();
        response_.clear();
        residual_.clear();
        if (left())
            left()->drain();
        if (right())
            right()->drain();
    }

public:
    static void initial_residual(const XYSet& full_set,
        std::vector<double> * full_residual, double * y0)
    {
        assert(full_residual->empty());
        double positive  = 0.0;
        double total = 0.0;
        for (size_t i=0, s=full_set.size(); i<s; i++)
        {
            const XY& xy = full_set.get(i);
            double weight = xy.weight();
            if (xy.is_positive())
                positive += weight;
            total += weight;
        }

        double mean_y = positive / total;

        for (size_t i=0, s=full_set.size(); i<s; i++)
        {
            const XY& xy = full_set.get(i);
            full_residual->push_back(xy.y() - mean_y);
        }

        *y0 = mean_y;
    }

    static TreeLossNode * train(const XYSet& full_set, const TreeParam& param,
        std::vector<double> * full_residual)
    {
        assert(full_set.size() == full_residual->size());
        TreeLossNode * root = create_root(full_set, param, *full_residual);
        train(root);
        root->update_residual(full_set, full_residual);
        root->drain();
        return root;
    }

    // only for GBDTPredictor
    static TreeLossNode * create_for_predictor(const TreeParam& param)
    {
        TreeLossNode * node = new TreeLossNode(param, 0);
        node->left() = 0;
        node->right() = 0;
        return node;
    }

    double& total_loss() {return total_loss_;}
    double total_loss() const {return total_loss_;}
    double& loss() {return loss_;}
    double loss() const {return loss_;}

private:
    void loss_all(
        size_t * _split_x_index,
        kXType * _split_x_type,
        CompoundValue * _split_x_value,
        double * _y_left,
        double * _y_right,
        double * min_loss) const
    {
        *min_loss = std::numeric_limits<double>::max();
        for (size_t x_index=0, s=set().get_xtype_size(); x_index<s; x_index++)
        {
            kXType x_type = set().get_xtype(x_index);
            CompoundValue x_value;
            double y_left = 0.0;
            double y_right = 0.0;
            double loss;
            loss_X(x_index, x_type, &x_value, &y_left, &y_right, &loss);
            if (loss < *min_loss)
            {
                *_split_x_index = x_index;
                *_split_x_type = x_type;
                *_split_x_value = x_value;
                *_y_left = y_left;
                *_y_right = y_right;
                *min_loss = loss;
            }
        }
    }

    void loss_X(
        size_t _split_x_index,
        kXType _split_x_type,
        CompoundValue * _split_x_value,
        double * _y_left,
        double * _y_right,
        double * min_loss) const
    {
        CompoundValueVector x_values;
        get_unique_x_values(_split_x_index, param().max_x_values_number, &x_values);
        *min_loss = std::numeric_limits<double>::max();
        for (size_t i=0, s=x_values.size(); i<s; i++)
        {
            CompoundValue x_value = x_values[i];
            double y_left;
            double y_right;
            double loss;
            loss_x(_split_x_index, _split_x_type, x_value, &y_left, &y_right, &loss);
            if (loss < *min_loss)
            {
                *_split_x_value = x_value;
                *_y_left = y_left;
                *_y_right = y_right;
                *min_loss = loss;
            }
        }
    }

    void loss_x(
        size_t _split_x_index,
        kXType _split_x_type,
        CompoundValue _split_x_value,
        double * _y_left,
        double * _y_right,
        double * loss) const
    {
        if (_split_x_type == kXType_Numerical)
            loss_x_numerical(_split_x_index, _split_x_value.d(), _y_left, _y_right, loss);
        else
            loss_x_category(_split_x_index, _split_x_value.i(), _y_left, _y_right, loss);
    }

    void loss_x_numerical(
        size_t _split_x_index,
        double _split_x_value,
        double * _y_left,
        double * _y_right,
        double * loss) const
    {
        double n_left = 0.0;
        double n_right = 0.0;
        double y_left = 0.0;
        double y_right = 0.0;

        for (size_t i=0, s=set().size(); i<s; i++)
        {
            const XY& xy = set().get(i);
            double x = xy.x(_split_x_index).d();
            double weight = xy.weight();
            double response = response_[i];
            if (x <= _split_x_value)
            {
                y_left += response * weight;
                n_left += weight;
            }
            else
            {
                y_right += response * weight;
                n_right += weight;
            }
        }

        if (n_left > EPS)
            y_left /= n_left;
        if (n_right > EPS)
            y_right /= n_right;

        *_y_left = y_left;
        *_y_right = y_right;
        __loss_x_numerical(_split_x_index, _split_x_value, y_left, y_right, loss);
    }

    void loss_x_category(
        size_t _split_x_index,
        int _split_x_value,
        double * _y_left,
        double * _y_right,
        double * loss) const
    {
        double n_left = 0.0;
        double n_right = 0.0;
        double y_left = 0.0;
        double y_right = 0.0;

        for (size_t i=0, s=set().size(); i<s; i++)
        {
            const XY& xy = set().get(i);
            int x = xy.x(_split_x_index).i();
            double weight = xy.weight();
            double response = response_[i];
            if (x == _split_x_value)
            {
                y_left += response * weight;
                n_left += weight;
            }
            else
            {
                y_right += response * weight;
                n_right += weight;
            }
        }

        if (n_left > EPS)
            y_left /= n_left;
        if (n_right > EPS)
            y_right /= n_right;

        *_y_left = y_left;
        *_y_right = y_right;
        __loss_x_category(_split_x_index, _split_x_value, y_left, y_right, loss);
    }

    void __loss_x_numerical(
        size_t _split_x_index,
        double _split_x_value,
        double _y_left,
        double _y_right,
        double * loss) const
    {
        *loss = 0.0;
        for (size_t i=0, s=set().size(); i<s; i++)
        {
            const XY& xy = set().get(i);
            double x = xy.x(_split_x_index).d();
            double weight = xy.weight();
            double diff;
            if (x <= _split_x_value)
                diff = response_[i] - _y_left;
            else
                diff = response_[i] - _y_right;
            // weighted square loss
            *loss += (diff * diff * weight);
        }
    }

    void __loss_x_category(
        size_t _split_x_index,
        int _split_x_value,
        double _y_left,
        double _y_right,
        double * loss) const
    {
        *loss = 0.0;
        for (size_t i=0, s=set().size(); i<s; i++)
        {
            const XY& xy = set().get(i);
            int x = xy.x(_split_x_index).i();
            double weight = xy.weight();
            double diff;
            if (x == _split_x_value)
                diff = response_[i] - _y_left;
            else
                diff = response_[i] - _y_right;
            // weighted square loss
            *loss += (diff * diff * weight);
        }
    }
};

const TreeParam GBDTPredictor::empty_param_;

GBDTPredictor::GBDTPredictor() {}

GBDTPredictor::~GBDTPredictor() {clear();}

double GBDTPredictor::predict(const CompoundValueVector& X) const
{
    assert(!trees_.empty());
    double y = y0_;
    for (size_t i=0, s=trees_.size(); i<s; i++)
        y += trees_[i]->predict(X);
    return y;
}

void GBDTPredictor::clear()
{
    for (size_t i=0, s=trees_.size(); i<s; i++)
        delete trees_[i];
}

GBDTTrainer::GBDTTrainer(const XYSet& set, const TreeParam& param)
    : full_set_(set), param_(param), full_residual_() {}

GBDTTrainer::~GBDTTrainer() {}

double GBDTTrainer::square_loss() const
{
    assert(full_set_.size() == full_residual_.size());
    double loss = 0.0;
    for (size_t i=0, s=full_set_.size(); i<s; i++)
    {
        const XY& xy = full_set_.get(i);
        double res = full_residual_[i];
        loss += (res * res * xy.weight());
    }
    return loss;
}

static void record_loss_drop(const TreeLossNode * node,
                             double current_loss,
                             std::vector<double> * loss_drop_vector)
{
    if (node->is_leaf())
        return;

    double& loss_drop = (*loss_drop_vector)[node->split_x_index()];
    double drop = current_loss - node->loss();
    loss_drop += drop;

    record_loss_drop(node->left(), node->loss(), loss_drop_vector);
    record_loss_drop(node->right(), node->loss(), loss_drop_vector);
}

void GBDTTrainer::dump_feature_importance() const
{
    if (param_.gbdt_sample_rate != 1.0)
        printf("sample rate is not 1.0, feature importance is unfair\n");

    std::vector<double> loss_drop_vector;
    loss_drop_vector.resize(full_set_.get_xtype_size(), 0.0);

    for (size_t i=0; i<trees_.size(); i++)
        record_loss_drop(trees_[i], trees_[i]->total_loss(), &loss_drop_vector);

    double total_drop = 0.0;
    for (size_t i=0; i<loss_drop_vector.size(); i++)
        total_drop += loss_drop_vector[i];
    for (size_t i=0; i<loss_drop_vector.size(); i++)
        printf("feature %d importance: %lf\n", (int)i, loss_drop_vector[i] / total_drop);
}

void GBDTTrainer::train()
{
    assert(trees_.empty());

    TreeLossNode::initial_residual(full_set_, &full_residual_, &y0_);
    if (param_.verbose)
        printf("square_loss=%lf\n", square_loss());

    for (size_t i=0; i<param_.gbdt_tree_number; i++)
    {
        printf("training tree No.%d... ", (int)i);
        TreeLossNode * tree = TreeLossNode::train(full_set_, param_, &full_residual_);
        trees_.push_back(tree);
        if (param_.verbose)
        {
            double total_loss = square_loss();
            tree->total_loss() = total_loss;
            printf("square_loss=%lf\n", total_loss);
        }
        printf("OK\n");
    }

    if (param_.verbose)
        dump_feature_importance();
}

/************************************************************************/
/* JSON part */
/************************************************************************/
#if defined NDEBUG
# define RAPID_JSON_CHECK_HAS_MEMBER(value, member)
#else
# define RAPID_JSON_CHECK_HAS_MEMBER(value, member) \
    do if (!value.HasMember(member)) {fprintf(stderr, "should have member: %s\n", member); return -1;} \
    while (0)
#endif

using namespace rapidjson;

static int load_tree(const TreeParam& param, const Value& tree, TreeLossNode * node)
{
    if (tree.HasMember("value"))
    {
        node->leaf() = true;
        node->y() = tree["value"].GetDouble();
    }
    else
    {
        node->leaf() = false;

        RAPID_JSON_CHECK_HAS_MEMBER(tree, "split_index");
        RAPID_JSON_CHECK_HAS_MEMBER(tree, "split_type");
        RAPID_JSON_CHECK_HAS_MEMBER(tree, "split_value");
        RAPID_JSON_CHECK_HAS_MEMBER(tree, "left");
        RAPID_JSON_CHECK_HAS_MEMBER(tree, "right");

        node->split_x_index() = tree["split_index"].GetInt();
        const char * type = tree["split_type"].GetString();
        if (strcmp(type, "numerical") == 0)
        {
            node->split_x_type() = kXType_Numerical;
            node->split_x_value().d() = tree["split_value"].GetDouble();
        }
        else if (strcmp(type, "category") == 0)
        {
            node->split_x_type() = kXType_Category;
            node->split_x_value().i() = tree["split_value"].GetInt();
        }
        else
        {
            fprintf(stderr, "invalid type: %s\n", type);
            return -1;
        }

        const Value& left = tree["left"];
        TreeLossNode * left_node = TreeLossNode::create_for_predictor(param);
        if (load_tree(param, left, left_node) == -1)
        {
            delete left_node;
            return -1;
        }
        else
        {
            node->left() = left_node;
        }

        const Value& right = tree["right"];
        TreeLossNode * right_node = TreeLossNode::create_for_predictor(param);
        if (load_tree(param, right, right_node) == -1)
        {
            delete right_node;
            return -1;
        }
        else
        {
            node->right() = right_node;
        }
    }

    return 0;
}

int GBDTPredictor::load_json(FILE * fp)
{
    assert(trees_.empty());
    FileStream stream(fp);
    Document document;
    document.ParseStream<0>(stream);
    if (document.HasParseError())
    {
        fprintf(stderr, "parse json error: %s\n", document.GetParseError());
        return -1;
    }

    RAPID_JSON_CHECK_HAS_MEMBER(document, "y0");
    RAPID_JSON_CHECK_HAS_MEMBER(document, "trees");

    y0_ = document["y0"].GetDouble();

    const Value& trees = document["trees"];
    for (SizeType i=0, s=trees.Size(); i<s; i++)
    {
        const Value& tree = trees[i];
        TreeLossNode * node = TreeLossNode::create_for_predictor(empty_param_);
        if (load_tree(empty_param_, tree, node) == -1)
        {
            delete node;
            clear();
            return -1;
        }
        trees_.push_back(node);
    }

    return 0;
}

static void save_spec(const XYSpec& spec, Value * spec_value,
                      Document::AllocatorType& allocator)
{
    spec_value->SetArray();
    for (size_t i=0, s=spec.get_xtype_size(); i<s; i++)
    {
        kXType x_type = spec.get_xtype(i);
        spec_value->PushBack((x_type == kXType_Numerical) ? "numerical" : "category",
            allocator);
    }
}

static void save_tree(const TreeLossNode& tree, Value * tree_value,
                      Document::AllocatorType& allocator)
{
    tree_value->SetObject();
    if (tree.is_leaf())
    {
        tree_value->AddMember("value", tree.y(), allocator);
    }
    else
    {
        tree_value->AddMember("split_index", (int)tree.split_x_index(), allocator);

        bool numerical = tree.split_is_numerical();
        if (numerical)
        {
            tree_value->AddMember("split_type", "numerical", allocator);
            tree_value->AddMember("split_value", tree.split_get_double(), allocator);
        }
        else
        {
            tree_value->AddMember("split_type", "category", allocator);
            tree_value->AddMember("split_value", tree.split_get_int(), allocator);
        }

        Value left_value;
        save_tree(*tree.left(), &left_value, allocator);
        tree_value->AddMember("left", left_value, allocator);

        Value right_value;
        save_tree(*tree.right(), &right_value, allocator);
        tree_value->AddMember("right", right_value, allocator);
    }
}

void GBDTTrainer::save_json(FILE * fp) const
{
    FileStream stream(fp);
    Writer<FileStream> writer(stream);

    Document document;
    Document::AllocatorType& allocator = document.GetAllocator();
    document.SetObject();

    Value spec_value;
    save_spec(full_set_.spec(), &spec_value, allocator);
    document.AddMember("spec", spec_value, allocator);

    document.AddMember("y0", y0_, allocator);

    Value trees_value;
    trees_value.SetArray();
    for (size_t i=0, s=trees_.size(); i<s; i++)
    {
        Value tree_value;
        save_tree(*trees_[i], &tree_value, allocator);
        trees_value.PushBack(tree_value, allocator);
    }
    document.AddMember("trees", trees_value, allocator);

    document.Accept(writer);
}