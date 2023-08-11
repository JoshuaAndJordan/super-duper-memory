#!/usr/bin/env python
import os
from flask import Flask
from flask_migrate import Migrate
from flask_sqlalchemy import SQLAlchemy

migrate = Migrate()
db = SQLAlchemy()

class User(db.Model):
	__tablename__ = 'jd_users'
	ID = db.Column(db.Integer, primary_key=True, unique=True, index=True)
	first_name = db.Column(db.String(256), nullable=False, unique=False)
	last_name = db.Column(db.String(256), nullable=False, unique=False)
	email = db.Column(db.String(128), nullable=False, unique=True)
	username = db.Column(db.String(128), nullable=False, unique=True)
	address = db.Column(db.String(256), nullable=False, unique=False)
	password_hash = db.Column(db.String(64), nullable=False, unique=False)
  social_media = db.relationship('SocialMediaChannel', backref='user')
  pricing_tasks = db.relationship('AssignedPricingTask', backref='user')


class Subscription(db.Model):
	__tablename__ = 'jd_subscriptions'
	ID = db.Column(db.Integer, primary_key=True, unique=True, index=True)
	is_active = db.Column(db.Boolean, default=False)
	start_date = db.Column(db.DateTime, nullable=False, unique=False)
	end_date = db.Column(db.DateTime, nullable=True, unique=False)
	date_subscribed = db.Column(db.DateTime, nullable=False, unique=False)
	sub_type = db.Column(db.String(128), nullable=False, unique=False)
	users = db.relationship('User', backref='')


class SocialMediaChannel(db.Model):
  __tablename__ = 'jd_social_media'
  ID = db.Column(db.Integer, primary_key=True, unique=True, index=True)
  channel_name = db.Column(db.String(128), nullable=False, unique=False)


class AssignedPricingTask(db.Model):
	__tablename__ = 'jd_pricing_tasks'
	ID = db.Column(db.Integer, primary_key=True, unique=True, index=True)
	user_id = db.Column(db.Integer, db.ForeignKey('db_users.id'), nullable=False)
	symbols = db.Column(db.String(256), nullable=False, default='BTCUSDT')
	trade_types = db.Column(db.String(16), nullable=False, default='spot')
	interval_in_seconds = db.Column(db.Integer, unique=False, nullable=False)
	socials = db.Column(db.String(512), default='')


class CCRecordings(db.Model):
	__tablename__ = 'rv_cc_recordings'
	id = db.Column(db.Integer, primary_key=True, unique=True, index=True)
	staff_id = db.Column(db.Integer, db.ForeignKey('rv_users.id'), nullable=False)
	phone_number = db.Column(db.String(64), nullable=False)
	recording_filename = db.Column(db.String(128), unique=True, nullable=False)
	call_duration = db.Column(db.String(16), nullable=False)
	date_time_of_call = db.Column(db.String(32), unique=False, nullable=False)
	type_of_call = db.Column(db.String(16), unique=False, nullable=False)


def create_app(config_name):
	app = Flask(__name__)
	# apply configuration
	cfg = os.path.join(os.getcwd(), 'config', config_name + '.py')
	app.config.from_pyfile(cfg)
	
	# initialize extensions
	db.init_app(app)
	migrate.init_app(app, db)
	return app

app = create_app('development')


if __name__ == '__main__':
	with app.app_context():
		#db.drop_all()
		db.create_all()
		print('Created')
