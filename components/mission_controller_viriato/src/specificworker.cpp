/*
 *    Copyright (C) 2021 by YOUR NAME HERE
 *
 *    This file is part of RoboComp
 *
 *    RoboComp is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    RoboComp is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with RoboComp.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "specificworker.h"
#include <cppitertools/zip.hpp>
#include <cppitertools/sliding_window.hpp>
#include <cppitertools/enumerate.hpp>
#include <clocale>


/**
* \brief Default constructor
*/
SpecificWorker::SpecificWorker(TuplePrx tprx, bool startup_check) : GenericWorker(tprx)
{
	this->startup_check_flag = startup_check;
    QLoggingCategory::setFilterRules("*.debug=false\n");
}

/**
* \brief Default destructor
*/
SpecificWorker::~SpecificWorker()
{
	std::cout << "Destroying SpecificWorker" << std::endl;
	G->write_to_json_file("./"+agent_name+".json");
	G.reset();
}

bool SpecificWorker::setParams(RoboCompCommonBehavior::ParameterList params)
{
	agent_name = params["agent_name"].value;
	agent_id = stoi(params["agent_id"].value);
	tree_view = params["tree_view"].value == "true";
	graph_view = params["graph_view"].value == "true";
	qscene_2d_view = params["2d_view"].value == "true";
	osg_3d_view = params["3d_view"].value == "true";
    std::setlocale(LC_NUMERIC, "C");                // for std::to_string(float) to put dots instead of commas
	return true;
}

void SpecificWorker::initialize(int period)
{
	std::cout << "Initialize worker" << std::endl;
	this->Period = period;
	if(this->startup_check_flag)
	{
		this->startup_check();
	}
	else
	{
		timer.start(Period);
		// create graph
		G = std::make_shared<DSR::DSRGraph>(0, agent_name, agent_id, ""); // Init nodes
		std::cout<< __FUNCTION__ << "Graph loaded" << std::endl;  

		//dsr update signals
		connect(G.get(), &DSR::DSRGraph::update_node_signal, this, &SpecificWorker::add_or_assign_node_slot);
//		connect(G.get(), &DSR::DSRGraph::update_edge_signal, this, &SpecificWorker::add_or_assign_edge_slot);
//		connect(G.get(), &DSR::DSRGraph::update_attrs_signal, this, &SpecificWorker::add_or_assign_attrs_slot);
//		connect(G.get(), &DSR::DSRGraph::del_edge_signal, this, &SpecificWorker::del_edge_slot);
//		connect(G.get(), &DSR::DSRGraph::del_node_signal, this, &SpecificWorker::del_node_slot);

		// Graph viewer
		using opts = DSR::DSRViewer::view;
		int current_opts = 0;
		opts main = opts::none;
		if(tree_view)
		    current_opts = current_opts | opts::tree;
		if(graph_view)
		{
		    current_opts = current_opts | opts::graph;
		}
		if(qscene_2d_view)
        {
            current_opts = current_opts | opts::scene;
            main = opts::graph;
        }
		if(osg_3d_view)
		    current_opts = current_opts | opts::osg;

		graph_viewer = std::make_unique<DSR::DSRViewer>(this, G, current_opts, main);
		setWindowTitle(QString::fromStdString(agent_name + "-") + QString::number(agent_id));

        // 2D widget
        widget_2d = qobject_cast<DSR::QScene2dViewer *>(graph_viewer->get_widget(opts::scene));
        widget_2d->set_draw_laser(true);
        connect(widget_2d, SIGNAL(mouse_right_click(int, int, std::uint64_t)), this, SLOT(new_target_from_mouse(int, int, std::uint64_t)));

        // custom widget
        graph_viewer->add_custom_widget_to_dock("Pioneer Plan Controller", &custom_widget);
        connect(custom_widget.pushButton_start_mission, SIGNAL(clicked()), this, SLOT(slot_start_mission()));
        connect(custom_widget.pushButton_stop_mission, SIGNAL(clicked()), this, SLOT(slot_stop_mission()));
        connect(custom_widget.pushButton_cancel_mission, SIGNAL(clicked()), this, SLOT(slot_cancel_mission()));
        //connect(custom_widget.comboBox_select_target, SIGNAL(activated(int)), this, SLOT(slot_select_target_object(int)));

        // Ignore attributes
        G->set_ignored_attributes<cam_depth_att>();

        // Inner Api
        inner_eigen = G->get_inner_eigen_api();

        // get camera_api
        if(auto cam_node = G->get_node(viriato_head_camera_name); cam_node.has_value())
            cam_api = G->get_camera_api(cam_node.value());
        else
            qFatal("YoloV4_tracker terminate: could not find a camera node");

        // Update combo
        update_room_list();

        this->Period = period;
		timer.start(Period);
	}
}

void SpecificWorker::compute()
{
    // check for existing missions
    static Plan plan;
    if (auto plan_o = plan_buffer.try_get(); plan_o.has_value())
    {
        plan = plan_o.value();
        plan.print();
        custom_widget.current_plan->setPlainText(QString::fromStdString(plan.pprint()));
        plan.set_active(true);
    }
    if(plan.is_active())
    {
        if( auto path = path_buffer.try_get(); path.has_value())
        {
            if (path.value().size() < 2)
            {
                plan.set_active(false);
                send_command_to_robot(std::make_tuple(0.f, 0.f, 0.f));
                slot_stop_mission();
                if(custom_widget.checkBox_cyclic->isChecked())
                {
                    std::random_device rd; std::mt19937 mt(rd());
                    std::uniform_int_distribution<int> random_index(0, custom_widget.comboBox_select_target->count());
                    auto index = random_index(mt);
                    qInfo() << "new index selected" << index;
                    custom_widget.comboBox_select_target->setCurrentIndex(index);
                    slot_start_mission();
                }
            }
            else
                qInfo() << "Path size: " << path.value().size();
        }
    }
    else
    { // there should be a plan after a few seconds  }
    }
}

void SpecificWorker::create_mission(const QPointF &pos, std::uint64_t target_node_id)
{
    qInfo() << __FUNCTION__ << " Creating GOTO mission to " << pos;
    Plan plan;
    // we get the id of the object clicked from the 2D representation
    if (auto target_node = G->get_node(target_node_id); target_node.has_value())
    {
        stringstream location;
        //location.imbue(std::locale("en_US.UTF8"));
        location << "[" << std::to_string(pos.x()) << "," << std::to_string(pos.y()) << "," << 0 << "]";
        std::cout << __FUNCTION__ << location.str();
        const std::string plan_string = R"({"plan":[{"action":"goto","params":{"location":)" + location.str() + R"(,"object":")" + target_node->name() + "\"}}]}";
        std::cout << __FUNCTION__ << " " << plan_string << std::endl;
        plan = Plan(plan_string);
        plan_buffer.put(plan);
    }

    // Check if there is not 'intention' node yet in G
    if(auto mind = G->get_node(robot_mind_name); mind.has_value())
    {
        if (auto intention = G->get_node(current_intention_name); intention.has_value())
        {
            std::cout << __FUNCTION__ << " Adding plan to intention node " << plan.to_string() << std::endl;
            G->add_or_modify_attrib_local<current_intention_att>(intention.value(), plan.to_string());
            if (G->update_node(intention.value()))
                std::cout << __FUNCTION__ << " Node \"Intention\" successfully updated in G" << std::endl;
            else
                std::cout << __FILE__ << __FUNCTION__ << " Fatal error inserting_new 'intention' node" << std::endl;
        }
        else  // create a new node
        {
            DSR::Node intention_node = DSR::Node::create<intention_node_type>(current_intention_name);
            G->add_or_modify_attrib_local<parent_att>(intention_node, mind.value().id());
            G->add_or_modify_attrib_local<level_att>(intention_node, G->get_node_level(mind.value()).value() + 1);
            G->add_or_modify_attrib_local<pos_x_att>(intention_node, (float) -466);
            G->add_or_modify_attrib_local<pos_y_att>(intention_node, (float) 42);
            G->add_or_modify_attrib_local<current_intention_att>(intention_node, plan.to_string());
            if (std::optional<int> intention_node_id = G->insert_node(intention_node); intention_node_id.has_value())
            {
                std::cout << __FUNCTION__ << " Node \"Intention\" successfully inserted in G" << std::endl;
                // insert EDGE
                DSR::Edge edge = DSR::Edge::create<has_edge_type>(mind.value().id(), intention_node.id());
                if (G->insert_or_assign_edge(edge))
                    std::cout << __FUNCTION__ << " Edge \"has_type\" inserted in G" << std::endl;
                else
                    std::cout << __FILE__ << __FUNCTION__ << " Fatal error inserting new edge: " << mind.value().id() << "->" << intention_node_id.value()
                              << " type: has" << std::endl;
            } else
                std::cout << __FUNCTION__ << " Node \"Intention\" could NOT be inserted in G" << std::endl;
        }
    }
    else
    {
        std::cout << __FILE__ << __FUNCTION__ << " Fatal error. robot mind node not found. Terminating" << std::endl;
        std::terminate();
    }
}
void SpecificWorker::send_command_to_robot(const std::tuple<float, float, float> &speeds)   //adv, side, rot
{
    auto &[adv_, side_, rot_] = speeds;
    auto robot_node = G->get_node(robot_name);
    G->add_or_modify_attrib_local<robot_ref_adv_speed_att>(robot_node.value(),  (float)adv_);
    G->add_or_modify_attrib_local<robot_ref_rot_speed_att>(robot_node.value(), (float)rot_);
    G->add_or_modify_attrib_local<robot_ref_side_speed_att>(robot_node.value(),  (float)side_);
    G->update_node(robot_node.value());
}
/////////////////////////////////////////////////////////////////////////////////////////////
/// Asynchronous changes on G nodes from G signals
////////////////////////////////////////////////////////////////////////////////////////////
void SpecificWorker::add_or_assign_node_slot(const std::uint64_t id, const std::string &type)
{
   if (type == path_to_target_type_name)
    {
        if( auto path_to_target_node = G->get_node(id); path_to_target_node.has_value())
        {
            auto x_values_o = G->get_attrib_by_name<path_x_values_att>(path_to_target_node.value());
            auto y_values_o = G->get_attrib_by_name<path_y_values_att >(path_to_target_node.value());
            if(x_values_o.has_value() and y_values_o.has_value())
            {
                auto &x_values = x_values_o.value().get();
                auto &y_values = y_values_o.value().get();
                std::vector<Eigen::Vector3d> path; path.reserve(x_values.size());
                for(auto &&[p, q] : iter::zip(x_values,y_values))
                    path.emplace_back(Eigen::Vector3d(p, q, 0.f));
                path_buffer.put(path);
                draw_path(path, &widget_2d->scene);
            }
        }
    }
   else if (type == room_type_name)
       update_room_list();
    if (type == rgbd_type_name and id == cam_api->get_id())
    {
        if(auto cam_node = G->get_node(id); cam_node.has_value())
        {
            if (const auto g_image = G->get_attrib_by_name<cam_rgb_att>(cam_node.value()); g_image.has_value())
            {
                //cv::Mat img(cam_api->get_height(), cam_api->get_width(), CV_8UC3, const_cast<std::vector<uint8_t> &>(g_image.value().get()).data());
                //cv::Mat out;
                //cv::resize(img, out, cv::Size(custom_widget.label_rgb->width(), custom_widget.label_rgb->height()), 0, 0);
                const auto &image = const_cast<std::vector<uint8_t> &>(g_image.value().get()).data();
                auto qimage = QImage(image, cam_api->get_width(), cam_api->get_height(), QImage::Format_RGB888);
                auto qimage_s = qimage.scaled(custom_widget.width(), custom_widget.height(), Qt::KeepAspectRatioByExpanding);
                auto pix = QPixmap::fromImage(qimage_s);
                custom_widget.label_rgb->setPixmap(pix);
            }
            else
                qWarning() << __FUNCTION__ << "No cam_rgb_att found in camera " << cam_api->get_id();
        }
        else
            qWarning() << __FUNCTION__ << "No camera_node found in G";
    }
}

void SpecificWorker::update_room_list()
{
    auto nodes = G->get_nodes_by_type(std::string(room_type_name));
    QStringList node_names = QStringList();
    custom_widget.comboBox_select_target->clear();
    for (auto node : nodes)
        node_names.insert(0, QString::fromStdString(node.name()));
    custom_widget.comboBox_select_target->insertItems(0, node_names);
}

void SpecificWorker::draw_path(std::vector<Eigen::Vector3d> &path, QGraphicsScene* viewer_2d)
{
    static std::vector<QGraphicsLineItem *> scene_road_points;

    //clear previous points
    for (QGraphicsLineItem* item : scene_road_points)
        viewer_2d->removeItem((QGraphicsItem*)item);
    scene_road_points.clear();

    /// Draw all points
    QGraphicsLineItem *line1, *line2;
    std::string color;
    for(auto &&p_pair : iter::sliding_window(path, 2))
    {
        if(p_pair.size() < 2)
            continue;
        Mat::Vector2d a_point(p_pair[0].x(), p_pair[0].y());
        Mat::Vector2d b_point(p_pair[1].x(), p_pair[1].y());
        Mat::Vector2d dir = a_point - b_point;
        Mat::Vector2d dir_perp = dir.unitOrthogonal();
        Eigen::ParametrizedLine segment = Eigen::ParametrizedLine<double, 2>::Through(a_point, b_point);
        Eigen::ParametrizedLine<double, 2> segment_perp((a_point+b_point)/2, dir_perp);
        auto left = segment_perp.pointAt(50);
        auto right = segment_perp.pointAt(-50);
        QLineF qsegment(QPointF(a_point.x(), a_point.y()), QPointF(b_point.x(), b_point.y()));
        QLineF qsegment_perp(QPointF(left.x(), left.y()), QPointF(right.x(), right.y()));

	/*
        if(i == 1 or i == path.size()-1)
            color = "#00FF00"; //Green
    	*/

        line1 = viewer_2d->addLine(qsegment, QPen(QBrush(QColor(QString::fromStdString(color))), 20));
        line2 = viewer_2d->addLine(qsegment_perp, QPen(QBrush(QColor(QString::fromStdString("#F0FF00"))), 20));
        line1->setZValue(2000);
        line2->setZValue(2000);
        scene_road_points.push_back(line1);
        scene_road_points.push_back(line2);
    }
}

///////////////////////////////////////////////////////
//// Check new target from mouse
///////////////////////////////////////////////////////
void SpecificWorker::new_target_from_mouse(int pos_x, int pos_y, std::uint64_t id)
{
    qInfo() << __FUNCTION__ << " Creating GOTO mission to " << pos_x << pos_y;
    create_mission(QPointF(pos_x, pos_y), id);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// UI
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SpecificWorker::slot_start_mission()
{
    std::string name = custom_widget.comboBox_select_target->currentText().toStdString();
    qInfo() << __FUNCTION__ << "New mission to " << QString::fromStdString(name);

    if (auto node = G->get_node(name); node.has_value())
    {
        auto polygon_x = G->get_attrib_by_name<delimiting_polygon_x_att>(node.value());
        auto polygon_y = G->get_attrib_by_name<delimiting_polygon_y_att>(node.value());
        if (polygon_x.has_value() and polygon_y.has_value())
        {
            QPolygonF pol;
            for (auto &&[px, py] : iter::zip(polygon_x.value().get(), polygon_y.value().get()))
                pol << QPointF(px, py);
            QRectF rect = pol.boundingRect();
            QPointF pos = rect.center();
            if (custom_widget.checkBox_random->isChecked())
            {
                std::random_device rd;
                std::mt19937 mt(rd());
                std::uniform_int_distribution<int> dist_x(-rect.width()/3, rect.width()/3);
                std::uniform_int_distribution<int> dist_y(-rect.height()/3, rect.height()/3);
                pos.setX(pos.x()+dist_x(mt));
                pos.setY(pos.y()+dist_y(mt));
            }
            create_mission(pos, node.value().id());
        }
    }
}

void SpecificWorker::slot_stop_mission()
{
    qInfo() << __FUNCTION__  ;
    // Check if there is intention node in G
    if(auto intention = G->get_node(current_intention_name); intention.has_value())
    {
        if (auto path = G->get_node(current_path_name); path.has_value())
        {
            if (G->delete_node(path.value().id()))
                qInfo() << __FUNCTION__ << "Node " << QString::fromStdString(current_path_name) << " deleted ";
            else
                qInfo() << __FUNCTION__ << "Error deleting node " << QString::fromStdString(current_path_name);
        }
        if (G->delete_node(intention.value().id()))
            qInfo() << __FUNCTION__ << "Node " << QString::fromStdString(current_intention_name) << " deleted ";
        else
            qInfo() << __FUNCTION__ << "Error deleting node " << QString::fromStdString(current_intention_name);
    }
}

void SpecificWorker::slot_cancel_mission()
{
    slot_stop_mission();
}

int SpecificWorker::startup_check()
{
    std::cout << "Startup check" << std::endl;
    QTimer::singleShot(200, qApp, SLOT(quit()));
    return 0;
}
